#include "RogueDhcpDetector.h"
#include "../net/Ieee80211Frame.h"
#include "../net/WifiManager.h"
#include <cstring>

RogueDhcpDetector g_rogueDhcpDetector;

namespace {
constexpr uint16_t kEtherIpv4 = 0x0800;
constexpr uint8_t kIpProtoUdp = 17;
constexpr uint16_t kBootpServerPort = 67;
constexpr uint16_t kBootpClientPort = 68;
constexpr uint8_t kBootReply = 2;
constexpr uint8_t kDhcpMsgOffer = 2;
constexpr uint8_t kDhcpMsgAck = 5;
constexpr uint8_t kDhcpOptMsgType = 53;
constexpr uint8_t kDhcpOptPad = 0;
constexpr uint8_t kDhcpOptEnd = 255;
}  // namespace

void RogueDhcpDetector::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
    // Same permanent-task-that-idles pattern as WardrivingManager /
    // CdpLldpSniffer — the task lives forever and just spins on
    // vTaskDelay while _running is false; start()/stop() only flip the
    // flag, they never create/destroy the task.
    xTaskCreatePinnedToCore(&RogueDhcpDetector::taskEntry, "roguedhcp", 4096, this, 1, nullptr, 0);
}

void RogueDhcpDetector::start() { _running = true; }
void RogueDhcpDetector::stop() { _running = false; }

void RogueDhcpDetector::taskEntry(void* arg) {
    static_cast<RogueDhcpDetector*>(arg)->run();
}

void RogueDhcpDetector::run() {
    for (;;) {
        if (!_running) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        esp_wifi_set_promiscuous_rx_cb(&RogueDhcpDetector::promiscuousRxTrampoline);
        esp_wifi_set_promiscuous(true);
        notify("passive rogue-DHCP watch on");

        while (_running) vTaskDelay(pdMS_TO_TICKS(500));

        esp_wifi_set_promiscuous(false);
        notify("rogue-DHCP watch off");
    }
}

void RogueDhcpDetector::promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_DATA) return;
    auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
    if (!pkt) return;
    g_rogueDhcpDetector.onPromiscuousFrame(pkt->payload, pkt->rx_ctrl.sig_len);
}

void RogueDhcpDetector::onPromiscuousFrame(const uint8_t* p, uint16_t len) {
    ieee80211::ParsedDataFrame frame;
    if (!ieee80211::parseDataFrame(p, len, frame)) return;
    if (frame.protectedFrame) return;  // WPA-encrypted - nothing readable, same as ArpSpoofManager

    uint8_t oui[3];
    uint16_t protocolId, payloadOffset;
    if (!ieee80211::parseSnap(p, len, frame.payloadOffset, oui, protocolId, payloadOffset)) return;

    bool standardOui = (oui[0] == 0 && oui[1] == 0 && oui[2] == 0);
    if (!standardOui || protocolId != kEtherIpv4) return;

    // --- IPv4 header (parsed inline on purpose, not shared with
    // ArpSpoofManager: ~20 lines of well-understood, low-risk header
    // walking, and duplicating it here is safer than refactoring the
    // already-hardware-verified path in ArpSpoofManager for DRY's sake). ---
    const uint8_t* ip = p + payloadOffset;
    uint16_t ipAvail = len - payloadOffset;
    if (ipAvail < 20) return;
    uint8_t version = ip[0] >> 4;
    uint8_t ihl = (ip[0] & 0x0F) * 4;
    if (version != 4 || ihl < 20 || ipAvail < ihl) return;
    if (ip[9] != kIpProtoUdp) return;
    IPAddress srcIp(ip[12], ip[13], ip[14], ip[15]);

    // --- UDP header ---
    const uint8_t* udp = ip + ihl;
    uint16_t udpAvail = ipAvail - ihl;
    if (udpAvail < 8) return;
    uint16_t srcPort = ((uint16_t)udp[0] << 8) | udp[1];
    uint16_t dstPort = ((uint16_t)udp[2] << 8) | udp[3];
    // A DHCP server's reply travels 67 -> 68. Anything else on these
    // ports isn't a server-to-client BOOTP reply, so ignore it.
    if (srcPort != kBootpServerPort || dstPort != kBootpClientPort) return;

    // --- BOOTP/DHCP fixed header (RFC 2131) ---
    const uint8_t* dhcp = udp + 8;
    uint16_t dhcpAvail = udpAvail - 8;
    if (dhcpAvail < 240) return;                 // fixed part (236) + 4-byte magic cookie
    if (dhcp[0] != kBootReply) return;           // op == BOOTREPLY
    // Magic cookie at offset 236 marks this as DHCP (not plain BOOTP).
    if (!(dhcp[236] == 0x63 && dhcp[237] == 0x82 && dhcp[238] == 0x53 && dhcp[239] == 0x63)) return;

    IPAddress offeredIp(dhcp[16], dhcp[17], dhcp[18], dhcp[19]);  // yiaddr

    // --- Options (start at 240): find option 53 (DHCP Message Type). ---
    bool isServerReply = false;
    uint16_t i = 240;
    while (i < dhcpAvail) {
        uint8_t opt = dhcp[i];
        if (opt == kDhcpOptEnd) break;
        if (opt == kDhcpOptPad) { i++; continue; }
        if (i + 1 >= dhcpAvail) break;
        uint8_t optLen = dhcp[i + 1];
        if (i + 2 + optLen > dhcpAvail) break;   // malformed - stop
        if (opt == kDhcpOptMsgType && optLen >= 1) {
            uint8_t msgType = dhcp[i + 2];
            // OFFER and ACK are the two message types a server sends while
            // handing out configuration - either proves a DHCP server is
            // answering from this source IP.
            isServerReply = (msgType == kDhcpMsgOffer || msgType == kDhcpMsgAck);
            break;
        }
        i += 2 + optLen;
    }
    if (!isServerReply) return;

    addSighting(srcIp, offeredIp);
}

void RogueDhcpDetector::addSighting(const IPAddress& serverIp, const IPAddress& offeredIp) {
    // A DHCP server whose IP differs from the gateway this device is
    // actually using is the classic rogue-DHCP tell (a second server
    // racing the legitimate one, often to hand out itself as gateway/DNS
    // for a MITM). Not proof by itself - some networks legitimately run
    // DHCP on a host separate from the gateway - so this only flags it.
    IPAddress gw = g_wifi.gatewayIP();
    bool suspicious = (gw != IPAddress((uint32_t)0)) && (serverIp != gw);

    bool isNew = false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        Sighting* existing = nullptr;
        for (auto& s : _sightings) {
            if (s.serverIp == serverIp) {
                existing = &s;
                break;
            }
        }
        if (existing) {
            existing->offeredIp = offeredIp;
            existing->lastSeenMs = millis();
            existing->suspicious = suspicious;
        } else if (_sightings.size() < kMaxSightings) {
            Sighting s;
            s.serverIp = serverIp;
            s.offeredIp = offeredIp;
            s.lastSeenMs = millis();
            s.suspicious = suspicious;
            _sightings.push_back(s);
            isNew = true;
        }
        xSemaphoreGive(_mutex);
    }

    if (isNew) {
        notify(String(suspicious ? "ROGUE? " : "dhcp ") + serverIp.toString() +
               " offered " + offeredIp.toString());
    }
}

void RogueDhcpDetector::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::RogueDhcp;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

size_t RogueDhcpDetector::sightingCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _sightings.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool RogueDhcpDetector::getSighting(size_t index, Sighting& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _sightings.size();
    if (ok) out = _sightings[_sightings.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
