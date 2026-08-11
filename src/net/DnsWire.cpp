#include "DnsWire.h"

namespace {
void appendName(std::vector<uint8_t>& out, const String& dotted) {
    int start = 0;
    int total = (int)dotted.length();
    while (start < total) {
        int dot = dotted.indexOf('.', start);
        int end = (dot < 0) ? total : dot;
        out.push_back((uint8_t)(end - start));
        for (int i = start; i < end; i++) out.push_back((uint8_t)dotted[i]);
        start = end + 1;
    }
    out.push_back(0x00);
}
}  // namespace

std::vector<uint8_t> dnswire::buildQuery(const String& name, uint16_t qtype) {
    std::vector<uint8_t> pkt = {
        0x12, 0x34,              // txn id - arbitrary, callers here never correlate a reply to it
        0x01, 0x00,              // flags: standard query, recursion desired
        0x00, 0x01,              // qdcount = 1
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    appendName(pkt, name);
    pkt.push_back((uint8_t)(qtype >> 8));
    pkt.push_back((uint8_t)(qtype & 0xFF));
    pkt.push_back(0x00);
    pkt.push_back(0x01);  // qclass IN
    return pkt;
}

int dnswire::skipName(const uint8_t* buf, int len, int pos) {
    // Checks for a compression pointer at every label boundary, not
    // just the first one - a name can legally be a few inline labels
    // followed by a pointer (RFC 1035 §4.1.4), and once a pointer
    // appears the name is over from this "how many bytes did this
    // occupy right here" perspective, regardless of what came before it.
    while (pos < len) {
        uint8_t b = buf[pos];
        if (b == 0) return pos + 1;
        if ((b & 0xC0) == 0xC0) {
            return (pos + 2 <= len) ? pos + 2 : -1;
        }
        pos += b + 1;
    }
    return -1;
}

String dnswire::decodeName(const uint8_t* buf, int len, int pos) {
    String out;
    int jumps = 0;
    while (pos >= 0 && pos < len) {
        uint8_t b = buf[pos];
        if (b == 0) break;
        if ((b & 0xC0) == 0xC0) {
            if (pos + 1 >= len || ++jumps > 10) return "";
            pos = ((b & 0x3F) << 8) | buf[pos + 1];
            continue;
        }
        if (pos + 1 + b > len) return "";
        if (out.length()) out += '.';
        for (int i = 0; i < b; i++) out += (char)buf[pos + 1 + i];
        pos += 1 + b;
    }
    return out;
}
