#include "PassiveHostDiscovery.h"
#include "../net/Ieee80211Frame.h"
#include <Arduino.h>  // millis()
#include <cstring>

PassiveHostDiscovery g_passiveHostDiscovery;

namespace {
constexpr uint16_t kEtherIpv4 = 0x0800;
}  // namespace

void PassiveHostDiscovery::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
    // Permanent-task-that-idles pattern, same as CdpLldpSniffer /
    // RogueDhcpDetector — start()/stop() only flip _running.
    xTaskCreatePinnedToCore(&PassiveHostDiscovery::taskEntry, "passivehost", 4096, this, 1, nullptr, 0);
}

void PassiveHostDiscovery::start() { _running = true; }
void PassiveHostDiscovery::stop() { _running = false; }

void PassiveHostDiscovery::taskEntry(void* arg) {
    static_cast<PassiveHostDiscovery*>(arg)->run();
}

void PassiveHostDiscovery::run() {
    for (;;) {
        if (!_running) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        esp_wifi_set_promiscuous_rx_cb(&PassiveHostDiscovery::promiscuousRxTrampoline);
        esp_wifi_set_promiscuous(true);
        notify("passive host discovery on");

        while (_running) vTaskDelay(pdMS_TO_TICKS(500));

        esp_wifi_set_promiscuous(false);
        notify("passive host discovery off");
    }
}

void PassiveHostDiscovery::promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_DATA) return;
    auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
    if (!pkt) return;
    g_passiveHostDiscovery.onPromiscuousFrame(pkt->payload, pkt->rx_ctrl.sig_len);
}

void PassiveHostDiscovery::onPromiscuousFrame(const uint8_t* p, uint16_t len) {
    ieee80211::ParsedDataFrame frame;
    if (!ieee80211::parseDataFrame(p, len, frame)) return;
    if (frame.protectedFrame) return;  // WPA-encrypted - unreadable

    uint8_t oui[3];
    uint16_t protocolId, payloadOffset;
    if (!ieee80211::parseSnap(p, len, frame.payloadOffset, oui, protocolId, payloadOffset)) return;
    bool standardOui = (oui[0] == 0 && oui[1] == 0 && oui[2] == 0);
    if (!standardOui || protocolId != kEtherIpv4) return;

    const uint8_t* ip = p + payloadOffset;
    if ((uint16_t)(len - payloadOffset) < 20) return;
    if ((ip[0] >> 4) != 4) return;
    IPAddress srcIp(ip[12], ip[13], ip[14], ip[15]);
    // Skip 0.0.0.0 (unconfigured / DHCP-in-progress) and anything in the
    // 224.0.0.0/4 multicast range (not a real host address).
    if (srcIp[0] == 0 || srcIp[0] >= 224) return;

    observe(srcIp, frame.srcMac);
}

void PassiveHostDiscovery::observe(const IPAddress& ip, const uint8_t mac[6]) {
    bool isNew = false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        Observed* existing = nullptr;
        for (auto& o : _hosts) {
            if (o.ip == ip) {
                existing = &o;
                break;
            }
        }
        if (existing) {
            existing->frames++;
            existing->lastSeenMs = millis();
            if (!existing->macKnown) {
                memcpy(existing->mac, mac, 6);
                existing->macKnown = true;
            }
        } else if (_hosts.size() < kMaxHosts) {
            Observed o;
            o.ip = ip;
            memcpy(o.mac, mac, 6);
            o.macKnown = true;
            o.frames = 1;
            o.lastSeenMs = millis();
            _hosts.push_back(o);
            isNew = true;
        }
        xSemaphoreGive(_mutex);
    }

    if (isNew) notify(String("host: ") + ip.toString());
}

void PassiveHostDiscovery::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::PassiveHost;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

size_t PassiveHostDiscovery::count() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _hosts.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool PassiveHostDiscovery::get(size_t index, Observed& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _hosts.size();
    if (ok) out = _hosts[_hosts.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
