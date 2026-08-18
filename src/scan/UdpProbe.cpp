#include "UdpProbe.h"
#include "../net/DnsWire.h"
#include <WiFiUdp.h>

namespace {

// Sends `packet` to ip:port over UDP and waits up to timeoutMs for ANY
// reply. Content isn't validated - a reply at all is strong enough
// evidence something is listening and speaking the right protocol,
// since these packets are shaped as real protocol requests rather than
// random bytes an unrelated service would be unlikely to acknowledge.
bool sendAndWaitForReply(const IPAddress& ip, uint16_t port, const uint8_t* packet, size_t len, uint16_t timeoutMs) {
    WiFiUDP udp;
    if (!udp.begin(0)) return false;  // bind an ephemeral local port

    udp.beginPacket(ip, port);
    udp.write(packet, len);
    udp.endPacket();

    uint32_t start = millis();
    int packetSize = 0;
    while ((millis() - start) < timeoutMs) {
        packetSize = udp.parsePacket();
        if (packetSize > 0) break;
        delay(10);
    }
    udp.stop();
    return packetSize > 0;
}

bool probeDns(const IPAddress& ip, uint16_t timeoutMs) {
    // A syntactically valid A-record query for a name that will never
    // resolve to anything real - any actual DNS server still answers
    // it (NXDOMAIN or similar), which is all we need to know something
    // is listening on 53/udp.
    std::vector<uint8_t> pkt = dnswire::buildQuery("cardputer.probe", /*qtype A=*/1);
    return sendAndWaitForReply(ip, 53, pkt.data(), pkt.size(), timeoutMs);
}

bool probeNtp(const IPAddress& ip, uint16_t timeoutMs) {
    // Standard 48-byte SNTP client request: first byte 0x1B = LI=0,
    // VN=3, Mode=3 (client) - the same first byte real `sntp`/`ntpdate`
    // clients send.
    uint8_t pkt[48] = {0};
    pkt[0] = 0x1B;
    return sendAndWaitForReply(ip, 123, pkt, sizeof(pkt), timeoutMs);
}

bool probeSnmp(const IPAddress& ip, uint16_t timeoutMs) {
    // Fixed SNMPv1 GetRequest for sysDescr.0 (OID 1.3.6.1.2.1.1.1.0),
    // community "public", request-id 1 - hand-built BER/ASN.1 and
    // verified byte-for-byte (outer SEQUENCE length + OID round-trip
    // decode) with a throwaway Python encoder/decoder before being
    // hardcoded here, the same discipline this codebase applies to
    // every other binary format it embeds (see OuiDatabase/
    // PortServiceDb's generator tools).
    // clang-format off
    static const uint8_t kGetSysDescr[] = {
        0x30, 0x26,                                                     // SEQUENCE, len 38
          0x02, 0x01, 0x00,                                             // version: INTEGER 0 (SNMPv1)
          0x04, 0x06, 'p', 'u', 'b', 'l', 'i', 'c',                     // community: OCTET STRING "public"
          0xA0, 0x19,                                                   // GetRequest-PDU, len 25
            0x02, 0x01, 0x01,                                           // request-id: INTEGER 1
            0x02, 0x01, 0x00,                                           // error-status: INTEGER 0
            0x02, 0x01, 0x00,                                           // error-index: INTEGER 0
            0x30, 0x0E,                                                 // varbind-list SEQUENCE, len 14
              0x30, 0x0C,                                               // varbind SEQUENCE, len 12
                0x06, 0x08, 0x2B, 0x06, 0x01, 0x02, 0x01, 0x01, 0x01, 0x00,  // OID 1.3.6.1.2.1.1.1.0
                0x05, 0x00,                                             // NULL
    };
    // clang-format on
    return sendAndWaitForReply(ip, 161, kGetSysDescr, sizeof(kGetSysDescr), timeoutMs);
}

}  // namespace

void UdpProbe::probeCommonServices(const IPAddress& ip, uint16_t timeoutMs, std::vector<PortResult>& results) {
    if (probeDns(ip, timeoutMs)) {
        PortResult r;
        r.port = 53;
        r.open = true;
        r.isUdp = true;
        r.service = "domain";  // IANA's registered name for port 53
        results.push_back(r);
    }
    if (probeNtp(ip, timeoutMs)) {
        PortResult r;
        r.port = 123;
        r.open = true;
        r.isUdp = true;
        r.service = "ntp";
        results.push_back(r);
    }
    if (probeSnmp(ip, timeoutMs)) {
        PortResult r;
        r.port = 161;
        r.open = true;
        r.isUdp = true;
        r.service = "snmp";
        results.push_back(r);
    }
}
