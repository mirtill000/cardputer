#include "CdpLldpSniffer.h"
#include "../net/Ieee80211Frame.h"
#include <cstring>

CdpLldpSniffer g_cdpLldpSniffer;

namespace {
constexpr uint16_t kCdpProtocolId = 0x2000;
constexpr uint16_t kLldpEthertype = 0x88CC;
}  // namespace

void CdpLldpSniffer::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
    // Same permanent-task-that-idles pattern as WardrivingManager::begin
    // — see its comment for why this isn't created/destroyed per
    // start()/stop().
    xTaskCreatePinnedToCore(&CdpLldpSniffer::taskEntry, "cdplldp", 4096, this, 1, nullptr, 0);
}

void CdpLldpSniffer::start() { _running = true; }
void CdpLldpSniffer::stop() { _running = false; }

void CdpLldpSniffer::taskEntry(void* arg) {
    static_cast<CdpLldpSniffer*>(arg)->run();
}

void CdpLldpSniffer::run() {
    for (;;) {
        if (!_running) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        esp_wifi_set_promiscuous_rx_cb(&CdpLldpSniffer::promiscuousRxTrampoline);
        esp_wifi_set_promiscuous(true);
        notify("passive CDP/LLDP sniffing on");

        while (_running) vTaskDelay(pdMS_TO_TICKS(500));

        esp_wifi_set_promiscuous(false);
        notify("CDP/LLDP sniffing off");
    }
}

void CdpLldpSniffer::promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_DATA) return;
    auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
    if (!pkt) return;
    g_cdpLldpSniffer.onPromiscuousFrame(pkt->payload, pkt->rx_ctrl.sig_len);
}

void CdpLldpSniffer::onPromiscuousFrame(const uint8_t* p, uint16_t len) {
    ieee80211::ParsedDataFrame frame;
    if (!ieee80211::parseDataFrame(p, len, frame)) return;
    if (frame.protectedFrame) return;  // encrypted - nothing readable, same as ArpSpoofManager

    uint8_t oui[3];
    uint16_t protocolId, payloadOffset;
    if (!ieee80211::parseSnap(p, len, frame.payloadOffset, oui, protocolId, payloadOffset)) return;

    bool ciscoOui = (oui[0] == 0x00 && oui[1] == 0x00 && oui[2] == 0x0C);
    bool standardOui = (oui[0] == 0 && oui[1] == 0 && oui[2] == 0);

    if (ciscoOui && protocolId == kCdpProtocolId) {
        parseCdp(p + payloadOffset, len - payloadOffset);
    } else if (standardOui && protocolId == kLldpEthertype) {
        parseLldp(p + payloadOffset, len - payloadOffset);
    }
}

void CdpLldpSniffer::parseCdp(const uint8_t* p, uint16_t len) {
    // version(1) + ttl(1) + checksum(2), then a sequence of
    // type(2)+length(2, INCLUDING this 4-byte header)+value TLVs.
    if (len < 4) return;
    uint16_t pos = 4;

    String deviceId, portId;
    while (pos + 4 <= len) {
        uint16_t tlvType = ((uint16_t)p[pos] << 8) | p[pos + 1];
        uint16_t tlvLen = ((uint16_t)p[pos + 2] << 8) | p[pos + 3];
        if (tlvLen < 4 || (uint32_t)pos + tlvLen > len) break;  // malformed - stop, don't guess further

        uint16_t valueLen = tlvLen - 4;
        const uint8_t* value = p + pos + 4;

        if (tlvType == 0x0001) {  // Device ID
            deviceId = "";
            for (uint16_t i = 0; i < valueLen && i < 48; i++) deviceId += (char)value[i];
        } else if (tlvType == 0x0003) {  // Port ID
            portId = "";
            for (uint16_t i = 0; i < valueLen && i < 32; i++) portId += (char)value[i];
        }

        pos += tlvLen;
    }

    if (deviceId.length()) addOrUpdateNeighbor(deviceId, portId, /*isCdp=*/true);
}

void CdpLldpSniffer::parseLldp(const uint8_t* p, uint16_t len) {
    // Sequence of TLVs, no extra header: each is a 2-byte
    // (7-bit type << 9 | 9-bit length) field followed by that many
    // value bytes. Type 0 (End of LLDPDU, length 0) terminates early.
    uint16_t pos = 0;
    String chassisId, portId;

    while (pos + 2 <= len) {
        uint16_t tlvHeader = ((uint16_t)p[pos] << 8) | p[pos + 1];
        uint8_t tlvType = (uint8_t)(tlvHeader >> 9);
        uint16_t tlvLen = tlvHeader & 0x01FF;
        pos += 2;
        if (tlvType == 0 && tlvLen == 0) break;  // End of LLDPDU
        if ((uint32_t)pos + tlvLen > len) break;  // malformed - stop

        const uint8_t* value = p + pos;
        // First byte of Chassis ID / Port ID TLVs is a subtype
        // (MAC address, interface name, locally-assigned string, ...) -
        // skipped here rather than decoded per-subtype; the remaining
        // bytes are shown as best-effort text regardless of subtype,
        // which reads fine for the common text-based subtypes and just
        // looks like garbled text for the binary ones (e.g. a raw MAC
        // address) rather than crashing or misparsing anything.
        if (tlvType == 1 && tlvLen > 1) {  // Chassis ID
            chassisId = "";
            for (uint16_t i = 1; i < tlvLen && i < 49; i++) chassisId += (char)value[i];
        } else if (tlvType == 2 && tlvLen > 1) {  // Port ID
            portId = "";
            for (uint16_t i = 1; i < tlvLen && i < 33; i++) portId += (char)value[i];
        }

        pos += tlvLen;
    }

    if (chassisId.length()) addOrUpdateNeighbor(chassisId, portId, /*isCdp=*/false);
}

void CdpLldpSniffer::addOrUpdateNeighbor(const String& deviceId, const String& portId, bool isCdp) {
    bool isNew = false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        Neighbor* existing = nullptr;
        for (auto& n : _neighbors) {
            if (n.deviceId == deviceId && n.isCdp == isCdp) {
                existing = &n;
                break;
            }
        }
        if (existing) {
            existing->portId = portId;
            existing->lastSeenMs = millis();
        } else if (_neighbors.size() < kMaxNeighbors) {
            Neighbor n;
            n.deviceId = deviceId;
            n.portId = portId;
            n.isCdp = isCdp;
            n.lastSeenMs = millis();
            _neighbors.push_back(n);
            isNew = true;
        }
        xSemaphoreGive(_mutex);
    }

    if (isNew) notify(String(isCdp ? "CDP: " : "LLDP: ") + deviceId + (portId.length() ? (" (" + portId + ")") : ""));
}

void CdpLldpSniffer::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::CdpLldp;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

size_t CdpLldpSniffer::neighborCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _neighbors.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool CdpLldpSniffer::getNeighbor(size_t index, Neighbor& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _neighbors.size();
    if (ok) out = _neighbors[_neighbors.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
