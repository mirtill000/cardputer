#include "NtlmWire.h"
#include <cstring>

namespace {

// The one and only Type 1 message this module ever sends — fixed bytes,
// no field this firmware fills in ever varies. Signature(8) +
// MessageType=1(4) + NegotiateFlags=0x00808207(4) + DomainNameFields,
// all-zero(8) + WorkstationFields, all-zero(8) — see NtlmWire.h for what
// the flags mean. Verified against Python's ntlm-auth library's field
// layout before being pasted in here, same "hand the encoder nothing to
// get subtly wrong" reasoning as net/LdapWire.cpp's fixed request PDUs.
constexpr uint8_t kType1Negotiate[] = {
    0x4e, 0x54, 0x4c, 0x4d, 0x53, 0x53, 0x50, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x07, 0x82, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};  // 32 bytes

uint16_t readU16LE(const uint8_t* p, size_t off) {
    return (uint16_t)(p[off] | ((uint16_t)p[off + 1] << 8));
}

uint32_t readU32LE(const uint8_t* p, size_t off) {
    return (uint32_t)p[off] | ((uint32_t)p[off + 1] << 8) | ((uint32_t)p[off + 2] << 16) |
           ((uint32_t)p[off + 3] << 24);
}

// AV_PAIR values are UTF-16LE (MS-NLMP §2.2.2.1). Domain/hostnames are
// virtually always plain ASCII, so this takes the low byte of every
// 16-bit code unit and shows anything outside printable ASCII as '?'
// rather than pulling in a real UTF-16 decoder for the rare non-ASCII
// case — same defensive/best-effort convention BeaconProbeSniffer
// already uses for SSID bytes.
String decodeUtf16LeAscii(const uint8_t* p, uint16_t len) {
    String s;
    for (uint16_t i = 0; (uint16_t)(i + 1) < len; i = (uint16_t)(i + 2)) {
        char c = (char)p[i];
        s += (c >= 32 && c < 127) ? c : '?';
    }
    return s;
}

}  // namespace

std::vector<uint8_t> ntlmwire::buildType1Negotiate() {
    return std::vector<uint8_t>(kType1Negotiate, kType1Negotiate + sizeof(kType1Negotiate));
}

bool ntlmwire::parseType2Challenge(const uint8_t* buf, size_t len, Type2Info& out) {
    // Fixed header through TargetInfoFields (MS-NLMP §2.2.1.2): Signature(8)
    // + MessageType(4) + TargetNameFields(8) + NegotiateFlags(4) +
    // ServerChallenge(8) + Reserved(8) + TargetInfoFields(8) = 48 bytes.
    if (len < 48) return false;
    if (memcmp(buf, "NTLMSSP\0", 8) != 0) return false;
    if (readU32LE(buf, 8) != 2) return false;  // MessageType must be 2 (CHALLENGE_MESSAGE)

    uint16_t tiLen = readU16LE(buf, 40);
    uint32_t tiOff = readU32LE(buf, 44);

    // A valid Type 2 header with no usable TargetInfo (zero length, or an
    // offset/length that would read past the buffer - malformed or just
    // a server that didn't send one even though we asked) is still a
    // successful parse per this function's documented contract - `out`
    // simply stays at its default-constructed empty strings.
    if (tiLen == 0 || (uint64_t)tiOff + tiLen > len) return true;

    size_t pos = tiOff;
    size_t end = (size_t)tiOff + tiLen;
    while (pos + 4 <= end) {
        uint16_t avId = readU16LE(buf, pos);
        uint16_t avLen = readU16LE(buf, pos + 2);
        if (avId == 0) break;  // MsvAvEOL - end of TargetInfo
        if (pos + 4 + avLen > end) break;  // malformed - stop, keep whatever was already decoded

        const uint8_t* val = buf + pos + 4;
        switch (avId) {
            case 1: out.netbiosComputer = decodeUtf16LeAscii(val, avLen); break;  // MsvAvNbComputerName
            case 2: out.netbiosDomain = decodeUtf16LeAscii(val, avLen); break;    // MsvAvNbDomainName
            case 3: out.dnsComputer = decodeUtf16LeAscii(val, avLen); break;      // MsvAvDnsComputerName
            case 4: out.dnsDomain = decodeUtf16LeAscii(val, avLen); break;        // MsvAvDnsDomainName
            default: break;  // MsvAvDnsTreeName/Flags/Timestamp/... - not needed here
        }

        pos += (size_t)4 + avLen;
    }
    return true;
}
