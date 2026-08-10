#include "HostnameResolver.h"
#include <WiFiUdp.h>
#include <cstring>

// Why NBNS and not mDNS: arduino-esp32's ESPmDNS library is built to
// advertise *this* device's own name and browse services, not to send
// an arbitrary reverse query for someone else's IP — doing that would
// mean hand-rolling mDNS packet construction/parsing on top of raw UDP
// multicast, a materially bigger and riskier chunk of protocol code
// than NBNS turned out to be. NBNS (UDP/137) still covers a good chunk
// of a typical home/office LAN: Windows PCs, and NAS boxes/printers
// running Samba, all answer it. Devices that only speak mDNS (most
// phones, Macs, Chromecasts) simply won't get a hostname here — that's
// an accepted gap for this phase, not a bug.
namespace {

constexpr uint16_t kNbnsPort = 137;

// Fixed 50-byte NBNS "Node Status" query: 12-byte header (txn id 0,
// QDCOUNT=1) + a 38-byte question asking for NBSTAT on the wildcard
// name "*". NetBIOS first-level-encodes each nibble of the 16-byte name
// as a letter 'A'+nibble; the wildcard name is '*' (0x2A) followed by
// 15 NUL bytes, which encodes to "CKAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" (32
// chars) — derived from the encoding algorithm and checked with a
// throwaway script before being hardcoded here, not copied from memory.
const uint8_t kNbstatQuery[50] = {
    0x00, 0x00,  // transaction id
    0x00, 0x00,  // flags
    0x00, 0x01,  // qdcount = 1
    0x00, 0x00,  // ancount
    0x00, 0x00,  // nscount
    0x00, 0x00,  // arcount
    0x20,        // question name length = 32
    'C', 'K', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A',
    'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A',
    0x00,        // name terminator
    0x00, 0x21,  // qtype = NBSTAT
    0x00, 0x01,  // qclass = IN
};

// Skips a (possibly-compressed) DNS/NBNS name field starting at
// buf[pos], returning the offset just past it, or -1 if the buffer runs
// out first. Needed because the answer's RR_NAME may repeat the
// question name in full or replace it with a 2-byte compression pointer
// (RFC 1035 §4.1.4) — both are legal, and real NBNS responders use both
// in the wild.
int skipName(const uint8_t* buf, int len, int pos) {
    if (pos >= len) return -1;
    if ((buf[pos] & 0xC0) == 0xC0) {
        return (pos + 2 <= len) ? pos + 2 : -1;
    }
    while (pos < len && buf[pos] != 0) {
        pos += buf[pos] + 1;
    }
    return (pos < len) ? pos + 1 : -1;
}

}  // namespace

String HostnameResolver::resolve(const IPAddress& ip, uint16_t timeoutMs) {
    WiFiUDP udp;
    if (!udp.begin(0)) return "";  // bind an ephemeral local port

    udp.beginPacket(ip, kNbnsPort);
    udp.write(kNbstatQuery, sizeof(kNbstatQuery));
    udp.endPacket();

    uint32_t start = millis();
    int packetSize = 0;
    while ((millis() - start) < timeoutMs) {
        packetSize = udp.parsePacket();
        if (packetSize > 0) break;
        delay(10);
    }
    if (packetSize <= 0) {
        udp.stop();
        return "";
    }

    uint8_t buf[512];
    int cap = (packetSize > (int)sizeof(buf)) ? (int)sizeof(buf) : packetSize;
    int len = udp.read(buf, cap);
    udp.stop();
    if (len < 12) return "";

    uint16_t ancount = ((uint16_t)buf[6] << 8) | buf[7];
    if (ancount == 0) return "";

    int pos = 12;
    pos = skipName(buf, len, pos);  // question name
    if (pos < 0 || pos + 4 > len) return "";
    pos += 4;  // qtype + qclass

    pos = skipName(buf, len, pos);  // answer RR name
    if (pos < 0 || pos + 10 > len) return "";
    pos += 2;  // type
    pos += 2;  // class
    pos += 4;  // ttl
    uint16_t rdlength = ((uint16_t)buf[pos] << 8) | buf[pos + 1];
    pos += 2;
    if (rdlength < 1 || pos + rdlength > len) return "";

    uint8_t numNames = buf[pos];
    int nameStart = pos + 1;
    if (numNames == 0 || nameStart + 16 > len) return "";

    // First entry is conventionally the primary NetBIOS computer name:
    // 15 space-padded ASCII bytes followed by a 1-byte name-type suffix.
    char name[16] = {0};
    memcpy(name, buf + nameStart, 15);
    name[15] = '\0';
    for (int i = 14; i >= 0 && name[i] == ' '; i--) name[i] = '\0';

    if (name[0] == '\0') return "";
    return String(name);
}
