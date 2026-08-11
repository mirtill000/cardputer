#include "MdnsReverseResolver.h"
#include "../net/DnsWire.h"
#include <WiFiUdp.h>

namespace {
constexpr uint16_t kMdnsPort = 5353;
}  // namespace

String MdnsReverseResolver::resolve(const IPAddress& ip, uint16_t timeoutMs) {
    IPAddress mcast(224, 0, 0, 251);

    WiFiUDP udp;
    // Binds local port 5353 and joins the mDNS multicast group - a
    // compliant responder answers a query that arrived on 5353 by
    // multicasting its reply back to 224.0.0.251:5353, not by unicasting
    // to whatever ephemeral port sent it. This mirrors what real one-
    // shot mDNS query tools do (e.g. `dig -x <ip> @224.0.0.251 -p 5353`).
    //
    // RISK: this 2-arg beginMulticast(multicastAddr, port) overload is
    // this module's biggest single unknown (bigger than the reverse-PTR
    // parsing above, which was verified against a Python reference - see
    // DnsWire.h). Arduino-ESP32's WiFiUdp has carried a 3-arg
    // ESP8266-style beginMulticast(interfaceAddr, multicastAddr, port)
    // in some core versions; if this line fails to compile, that's the
    // fix - pass WiFi.localIP() as the first argument.
    if (!udp.beginMulticast(mcast, kMdnsPort)) return "";

    // Reverse-mapping name for ip, e.g. 192.168.1.10 ->
    // "10.1.168.192.in-addr.arpa" - RFC 6762 §3 lists in-addr.arpa
    // (among others) as one of the zones mDNS is used for directly, no
    // ".local" suffix needed.
    String reverseName = String(ip[3]) + "." + String(ip[2]) + "." + String(ip[1]) + "." + String(ip[0]) +
                          ".in-addr.arpa";
    std::vector<uint8_t> pkt = dnswire::buildQuery(reverseName, /*qtype PTR=*/12);

    udp.beginPacket(mcast, kMdnsPort);
    udp.write(pkt.data(), pkt.size());
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

    uint16_t qdcount = ((uint16_t)buf[4] << 8) | buf[5];
    uint16_t ancount = ((uint16_t)buf[6] << 8) | buf[7];
    if (ancount == 0) return "";

    int pos = 12;
    for (uint16_t i = 0; i < qdcount; i++) {
        pos = dnswire::skipName(buf, len, pos);
        if (pos < 0 || pos + 4 > len) return "";
        pos += 4;  // qtype + qclass
    }

    for (uint16_t i = 0; i < ancount; i++) {
        pos = dnswire::skipName(buf, len, pos);
        if (pos < 0 || pos + 10 > len) return "";

        uint16_t type = ((uint16_t)buf[pos] << 8) | buf[pos + 1];
        uint16_t rdlength = ((uint16_t)buf[pos + 8] << 8) | buf[pos + 9];
        pos += 10;  // type(2) + class(2) + ttl(4) + rdlength(2)
        if (pos + rdlength > len) return "";

        if (type == 12) {  // PTR
            String full = dnswire::decodeName(buf, len, pos);
            if (full.length()) {
                int dot = full.indexOf('.');
                return (dot > 0) ? full.substring(0, dot) : full;
            }
        }
        pos += rdlength;
    }

    return "";
}
