#include "VlanHopProbe.h"
#include "../net/Ieee80211Frame.h"
#include "../net/RawFrame.h"
#include "../core/Config.h"
#include <WiFi.h>
#include <Arduino.h>  // millis()
#include <cstring>

VlanHopProbe g_vlanHopProbe;

namespace {
constexpr uint16_t kEther8021Q = 0x8100;
}  // namespace

void VlanHopProbe::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
    // Permanent-task-that-idles pattern, same as PassiveHostDiscovery/
    // CdpLldpSniffer/OsFingerprint - start()/stop() only flip _running.
    xTaskCreatePinnedToCore(&VlanHopProbe::taskEntry, "vlanhop", 4096, this, 1, nullptr, 0);
}

void VlanHopProbe::start() { _running = true; }
void VlanHopProbe::stop() { _running = false; }

void VlanHopProbe::taskEntry(void* arg) {
    static_cast<VlanHopProbe*>(arg)->run();
}

void VlanHopProbe::run() {
    for (;;) {
        if (!_running) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        esp_wifi_set_promiscuous_rx_cb(&VlanHopProbe::promiscuousRxTrampoline);
        esp_wifi_set_promiscuous(true);
        notify("VLAN tag-leak listen on");

        while (_running) vTaskDelay(pdMS_TO_TICKS(500));

        esp_wifi_set_promiscuous(false);
        notify("VLAN tag-leak listen off");
    }
}

void VlanHopProbe::promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_DATA) return;
    auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
    if (!pkt) return;
    g_vlanHopProbe.onPromiscuousFrame(pkt->payload, pkt->rx_ctrl.sig_len);
}

void VlanHopProbe::onPromiscuousFrame(const uint8_t* p, uint16_t len) {
    ieee80211::ParsedDataFrame frame;
    if (!ieee80211::parseDataFrame(p, len, frame)) return;
    if (frame.protectedFrame) return;  // WPA-encrypted - unreadable

    uint8_t oui[3];
    uint16_t protocolId, payloadOffset;
    if (!ieee80211::parseSnap(p, len, frame.payloadOffset, oui, protocolId, payloadOffset)) return;
    bool standardOui = (oui[0] == 0 && oui[1] == 0 && oui[2] == 0);
    if (!standardOui || protocolId != kEther8021Q) return;

    if ((uint32_t)payloadOffset + 4 > len) return;  // need outer TCI(2) + next ethertype(2)
    uint16_t outerTci = (uint16_t)(((uint16_t)p[payloadOffset] << 8) | p[payloadOffset + 1]);
    uint16_t outerVlanId = outerTci & 0x0FFF;
    uint16_t nextEthertype = (uint16_t)(((uint16_t)p[payloadOffset + 2] << 8) | p[payloadOffset + 3]);

    bool doubleTagged = false;
    uint16_t innerVlanId = 0;
    if (nextEthertype == kEther8021Q && (uint32_t)payloadOffset + 8 <= len) {
        uint16_t innerTci = (uint16_t)(((uint16_t)p[payloadOffset + 4] << 8) | p[payloadOffset + 5]);
        innerVlanId = innerTci & 0x0FFF;
        doubleTagged = true;
    }

    observe(frame.srcMac, outerVlanId, doubleTagged, innerVlanId);
}

void VlanHopProbe::observe(const uint8_t mac[6], uint16_t outerVlanId, bool doubleTagged, uint16_t innerVlanId) {
    bool isNew = false;
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        TagSighting* existing = nullptr;
        for (auto& s : _sightings) {
            if (memcmp(s.mac, mac, 6) == 0) {
                existing = &s;
                break;
            }
        }
        if (existing) {
            existing->outerVlanId = outerVlanId;
            existing->doubleTagged = doubleTagged;
            existing->innerVlanId = innerVlanId;
            existing->count++;
            existing->lastSeenMs = millis();
        } else if (_sightings.size() < kMaxSightings) {
            TagSighting s;
            memcpy(s.mac, mac, 6);
            s.outerVlanId = outerVlanId;
            s.doubleTagged = doubleTagged;
            s.innerVlanId = innerVlanId;
            s.count = 1;
            s.lastSeenMs = millis();
            _sightings.push_back(s);
            isNew = true;
        }
        xSemaphoreGive(_mutex);
    }

    if (isNew) {
        String msg = "tag leak: vlan " + String(outerVlanId);
        if (doubleTagged) msg += " (double, inner " + String(innerVlanId) + ")";
        notify(msg);
    }
}

bool VlanHopProbe::sendDoubleTagProbe(uint16_t nativeVlanId, uint16_t targetVlanId) {
    // Defense in depth: the active send is gated in VlanHopScreen behind
    // OffensiveDisclaimerScreen, but never inject a crafted frame unless
    // the per-boot offensive consent flag is set.
    if (!g_config.offensiveEnabled) {
        notify("double-tag probe skipped: not authorized");
        return false;
    }
    // The frame embeds this device's own MAC/IP; without an up STA
    // interface those come back as zeros, producing a meaningless
    // "who has 0.0.0.0 from 00:00:..." frame. Skip rather than send
    // garbage onto the wire.
    if (WiFi.status() != WL_CONNECTED) {
        notify("double-tag probe skipped: WiFi not connected");
        return false;
    }

    uint8_t selfMac[6];
    WiFi.macAddress(selfMac);
    uint32_t selfIpRaw = (uint32_t)WiFi.localIP();

    uint8_t frame[50];
    memset(frame, 0, sizeof(frame));
    memset(frame + 0, 0xFF, 6);    // dst = broadcast - no way to target a specific host on a VLAN we can't see into
    memcpy(frame + 6, selfMac, 6);  // src = us

    uint16_t outerTci = nativeVlanId & 0x0FFF;
    uint16_t innerTci = targetVlanId & 0x0FFF;

    frame[12] = 0x81;
    frame[13] = 0x00;  // outer TPID (802.1Q)
    frame[14] = (uint8_t)(outerTci >> 8);
    frame[15] = (uint8_t)(outerTci & 0xFF);
    frame[16] = 0x81;
    frame[17] = 0x00;  // inner TPID (802.1Q) - the double-tagging trick itself
    frame[18] = (uint8_t)(innerTci >> 8);
    frame[19] = (uint8_t)(innerTci & 0xFF);
    frame[20] = 0x08;
    frame[21] = 0x06;  // ethertype = ARP

    frame[22] = 0x00;
    frame[23] = 0x01;  // htype = Ethernet
    frame[24] = 0x08;
    frame[25] = 0x00;  // ptype = IPv4
    frame[26] = 6;      // hlen
    frame[27] = 4;      // plen
    frame[28] = 0x00;
    frame[29] = 0x01;  // oper = request
    memcpy(frame + 30, selfMac, 6);
    memcpy(frame + 36, &selfIpRaw, 4);
    // tha (frame+42, 6 bytes) and tpa (frame+46, 4 bytes) left zeroed -
    // "who has 0.0.0.0" is a synthetic, harmless target: the actual ARP
    // content doesn't matter for testing whether a switch strips the
    // outer tag and forwards the inner one, only the tag structure does.

    bool ok = RawFrame::send(frame, sizeof(frame));
    notify(ok ? ("double-tag probe sent: native=" + String(nativeVlanId) + " target=" + String(targetVlanId))
              : "double-tag probe failed to send (netif not up?)");
    return ok;
}

void VlanHopProbe::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::VlanHop;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

size_t VlanHopProbe::sightingCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _sightings.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool VlanHopProbe::getSighting(size_t index, TagSighting& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _sightings.size();
    if (ok) out = _sightings[_sightings.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
