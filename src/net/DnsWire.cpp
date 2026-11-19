#include "DnsWire.h"

namespace {
// RFC 1035 §2.3.4 limits: a label is at most 63 bytes (its length byte
// has only 6 usable bits - the top two flag a compression pointer) and an
// encoded name at most 255 bytes. Names can come off the network
// (ServiceEnumerator re-queries discovered mDNS instance names), and the
// old encoder cast an over-long label length straight to uint8_t,
// silently emitting a malformed query. Empty labels ("a..b", a trailing
// '.') are skipped - a 0 length byte mid-name would terminate it early.
constexpr int kMaxLabelLen = 63;
constexpr size_t kMaxNameLen = 255;

bool appendName(std::vector<uint8_t>& out, const String& dotted) {
    size_t nameStart = out.size();
    int start = 0;
    int total = (int)dotted.length();
    while (start < total) {
        int dot = dotted.indexOf('.', start);
        int end = (dot < 0) ? total : dot;
        int labelLen = end - start;
        if (labelLen > kMaxLabelLen) return false;
        if (labelLen > 0) {
            out.push_back((uint8_t)labelLen);
            for (int i = start; i < end; i++) out.push_back((uint8_t)dotted[i]);
        }
        start = end + 1;
    }
    out.push_back(0x00);
    return (out.size() - nameStart) <= kMaxNameLen;
}
}  // namespace

std::vector<uint8_t> dnswire::buildQuery(const String& name, uint16_t qtype) {
    std::vector<uint8_t> pkt = {
        0x12, 0x34,              // txn id - arbitrary, callers here never correlate a reply to it
        0x01, 0x00,              // flags: standard query, recursion desired
        0x00, 0x01,              // qdcount = 1
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    if (!appendName(pkt, name)) return {};
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
