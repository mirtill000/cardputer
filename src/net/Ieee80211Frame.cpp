#include "Ieee80211Frame.h"
#include <cstring>

bool ieee80211::parseDataFrame(const uint8_t* p, uint16_t len, ParsedDataFrame& out) {
    if (len < 24) return false;

    uint8_t fc0 = p[0], fc1 = p[1];
    uint8_t type = (fc0 >> 2) & 0x3;
    uint8_t subtype = (fc0 >> 4) & 0xF;
    if (type != 2) return false;  // not a Data frame (skip mgmt/ctrl - beacons, probes, etc.)

    bool toDS = (fc1 & 0x01) != 0;
    bool fromDS = (fc1 & 0x02) != 0;
    if (toDS && fromDS) return false;  // 4-address WDS frame - rare, deliberately not handled

    out.protectedFrame = (fc1 & 0x40) != 0;

    bool isQos = (subtype & 0x08) != 0;
    uint16_t hdrLen = 24 + (isQos ? 2 : 0);
    if (len <= hdrLen) return false;

    const uint8_t* addr2 = p + 10;  // TA/SA
    const uint8_t* addr3 = p + 16;  // BSSID (usually)

    if (toDS && !fromDS) {
        memcpy(out.srcMac, addr2, 6);  // STA -> AP uplink: addr2 is the sender STA
    } else if (!toDS && fromDS) {
        memcpy(out.srcMac, addr3, 6);  // AP -> STA downlink: addr3 is the original sender
    } else {
        memcpy(out.srcMac, addr2, 6);  // IBSS/ad-hoc case
    }

    out.payloadOffset = hdrLen;
    return true;
}

bool ieee80211::parseSnap(const uint8_t* p, uint16_t len, uint16_t offset, uint8_t oui[3], uint16_t& protocolId,
                           uint16_t& payloadOffset) {
    if ((uint32_t)offset + 8 > len) return false;
    if (p[offset] != 0xAA || p[offset + 1] != 0xAA || p[offset + 2] != 0x03) return false;  // not standard SNAP

    oui[0] = p[offset + 3];
    oui[1] = p[offset + 4];
    oui[2] = p[offset + 5];
    protocolId = ((uint16_t)p[offset + 6] << 8) | p[offset + 7];
    payloadOffset = offset + 8;
    return true;
}
