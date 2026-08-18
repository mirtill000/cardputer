#include "OsFingerprint.h"
#include "../net/Ieee80211Frame.h"
#include <Arduino.h>  // millis()
#include <cstring>

OsFingerprint g_osFingerprint;

namespace {
constexpr uint16_t kEtherIpv4 = 0x0800;
constexpr uint8_t kIpProtoTcp = 6;

uint8_t ttlCeilOf(uint8_t ttl) {
    if (ttl <= 64) return 64;
    if (ttl <= 128) return 128;
    return 255;
}
}  // namespace

void OsFingerprint::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
    // Permanent-task-that-idles pattern, same as PassiveHostDiscovery/
    // CdpLldpSniffer/RogueDhcpDetector - start()/stop() only flip _running.
    xTaskCreatePinnedToCore(&OsFingerprint::taskEntry, "osfingerprint", 4096, this, 1, nullptr, 0);
}

void OsFingerprint::start() { _running = true; }
void OsFingerprint::stop() { _running = false; }

void OsFingerprint::taskEntry(void* arg) {
    static_cast<OsFingerprint*>(arg)->run();
}

void OsFingerprint::run() {
    for (;;) {
        if (!_running) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        esp_wifi_set_promiscuous_rx_cb(&OsFingerprint::promiscuousRxTrampoline);
        esp_wifi_set_promiscuous(true);
        notify("OS fingerprint listen on");

        while (_running) vTaskDelay(pdMS_TO_TICKS(500));

        esp_wifi_set_promiscuous(false);
        notify("OS fingerprint listen off");
    }
}

void OsFingerprint::promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_DATA) return;
    auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
    if (!pkt) return;
    g_osFingerprint.onPromiscuousFrame(pkt->payload, pkt->rx_ctrl.sig_len);
}

void OsFingerprint::onPromiscuousFrame(const uint8_t* p, uint16_t len) {
    ieee80211::ParsedDataFrame frame;
    if (!ieee80211::parseDataFrame(p, len, frame)) return;
    if (frame.protectedFrame) return;  // WPA-encrypted - unreadable

    uint8_t oui[3];
    uint16_t protocolId, payloadOffset;
    if (!ieee80211::parseSnap(p, len, frame.payloadOffset, oui, protocolId, payloadOffset)) return;
    bool standardOui = (oui[0] == 0 && oui[1] == 0 && oui[2] == 0);
    if (!standardOui || protocolId != kEtherIpv4) return;

    const uint8_t* ip = p + payloadOffset;
    uint16_t avail = (uint16_t)(len - payloadOffset);
    if (avail < 20) return;
    if ((ip[0] >> 4) != 4) return;

    uint8_t ihl = (uint8_t)((ip[0] & 0x0F) * 4);
    if (ihl < 20 || avail < (uint16_t)(ihl + 20)) return;
    if (ip[9] != kIpProtoTcp) return;

    const uint8_t* tcp = ip + ihl;
    uint16_t tcpAvail = (uint16_t)(avail - ihl);

    uint8_t flags = tcp[13];
    if ((flags & 0x12) != 0x12) return;  // must have both SYN and ACK set
    if (flags & 0x05) return;            // reject RST/FIN combos - malformed/irrelevant for a handshake reply

    uint8_t dataOffset = (uint8_t)((tcp[12] >> 4) * 4);
    if (dataOffset < 20 || tcpAvail < dataOffset) return;

    uint8_t ttl = ip[8];
    uint16_t window = (uint16_t)(((uint16_t)tcp[14] << 8) | tcp[15]);
    IPAddress srcIp(ip[12], ip[13], ip[14], ip[15]);

    // Walk the TCP options, recording each kind's single-letter code in
    // the order seen (RFC 9293 3.1's TCP option format: NOP is a bare
    // 1-byte kind; every other option is kind+length+data, length
    // counting itself). Malformed/truncated options just stop the walk
    // early - whatever was decoded before that point is still useful.
    String order;
    uint16_t optLen = (uint16_t)(dataOffset - 20);
    const uint8_t* opt = tcp + 20;
    uint16_t i = 0;
    while (i < optLen && order.length() < 32) {
        uint8_t kind = opt[i];
        if (kind == 0) break;  // end of options
        if (kind == 1) {
            order += 'N';  // NOP
            i += 1;
            continue;
        }
        if ((uint16_t)(i + 1) >= optLen) break;
        uint8_t optionLen = opt[i + 1];
        if (optionLen < 2 || (uint16_t)(i + optionLen) > optLen) break;
        switch (kind) {
            case 2: order += 'M'; break;  // MSS
            case 3: order += 'W'; break;  // Window Scale
            case 4: order += 'S'; break;  // SACK Permitted
            case 8: order += 'T'; break;  // Timestamps
            default: order += '?'; break;
        }
        i = (uint16_t)(i + optionLen);
    }

    observe(srcIp, frame.srcMac, ttl, window, order);
}

void OsFingerprint::observe(const IPAddress& ip, const uint8_t mac[6], uint8_t ttl, uint16_t window,
                             const String& optionOrder) {
    bool isNew = false;
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        Sighting* existing = nullptr;
        for (auto& s : _hosts) {
            if (s.ip == ip) {
                existing = &s;
                break;
            }
        }
        uint8_t ceil = ttlCeilOf(ttl);
        if (existing) {
            existing->ttl = ttl;
            existing->ttlCeil = ceil;
            existing->window = window;
            existing->optionOrder = optionOrder;
            existing->lastSeenMs = millis();
            if (!existing->macKnown) {
                memcpy(existing->mac, mac, 6);
                existing->macKnown = true;
            }
        } else if (_hosts.size() < kMaxHosts) {
            Sighting s;
            s.ip = ip;
            memcpy(s.mac, mac, 6);
            s.macKnown = true;
            s.ttl = ttl;
            s.ttlCeil = ceil;
            s.window = window;
            s.optionOrder = optionOrder;
            s.lastSeenMs = millis();
            _hosts.push_back(s);
            isNew = true;
        }
        xSemaphoreGive(_mutex);
    }

    if (isNew) notify(String("SYN-ACK: ") + ip.toString() + " ttl=" + String(ttl));
}

void OsFingerprint::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::OsFingerprint;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

size_t OsFingerprint::count() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _hosts.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool OsFingerprint::get(size_t index, Sighting& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _hosts.size();
    if (ok) out = _hosts[_hosts.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}

const char* OsFingerprint::ttlGuessLabel(uint8_t ttlCeil) {
    switch (ttlCeil) {
        case 64: return "Linux/BSD/macOS/Android";
        case 128: return "Windows";
        case 255: return "network gear/old Unix";
        default: return "unknown";
    }
}
