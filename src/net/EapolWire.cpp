#include "EapolWire.h"
#include "Ieee80211Frame.h"
#include <cstring>

namespace {
constexpr uint8_t kEapolTypeEapPacket = 0;  // EAPOL Type byte: 0=EAP-Packet, 1=Start, 2=Logoff, 3=Key, 5=ASF-Alert
constexpr uint8_t kEapolTypeKey = 3;
constexpr uint16_t kEtherTypeEapol = 0x888E;

constexpr uint8_t kEapCodeResponse = 2;  // EAP Code: 1=Request, 2=Response, 3=Success, 4=Failure
constexpr uint8_t kEapTypeIdentity = 1;  // EAP Type: 1=Identity, 2=Notification, 4=MD5, 25=PEAP, 21=TTLS, ...
constexpr uint16_t kMaxIdentityLen = 128;

// Fixed Key Descriptor layout, offsets relative to the EAPOL body
// (byte 0 = Version): Type(1)@1, Length(2)@2, DescriptorType(1)@4,
// KeyInformation(2)@5, KeyLength(2)@7, KeyReplayCounter(8)@9,
// KeyNonce(32)@17, KeyIV(16)@49, KeyRSC(8)@65, KeyID/Reserved(8)@73,
// KeyMIC(16)@81, KeyDataLength(2)@97, KeyData@99 - per IEEE 802.11-2020
// §12.7.2 (Figure 12-35 and surrounding text).
constexpr uint16_t kKeyInfoOffset = 5;
constexpr uint16_t kKeyDataLenOffset = 97;
constexpr uint16_t kKeyDataOffset = 99;

constexpr uint16_t kKeyInfoInstall = 0x0040;
constexpr uint16_t kKeyInfoAck = 0x0080;
constexpr uint16_t kKeyInfoMic = 0x0100;
constexpr uint16_t kKeyInfoSecure = 0x0200;
}  // namespace

eapol::Classification eapol::classify(const uint8_t* p, uint16_t len) {
    Classification c;

    ieee80211::ParsedDataFrame frame;
    if (!ieee80211::parseDataFrame(p, len, frame)) return c;
    if (frame.protectedFrame) return c;  // EAPOL is always sent in the clear - see header comment

    uint8_t oui[3];
    uint16_t protocolId, payloadOffset;
    if (!ieee80211::parseSnap(p, len, frame.payloadOffset, oui, protocolId, payloadOffset)) return c;

    bool standardOui = (oui[0] == 0 && oui[1] == 0 && oui[2] == 0);
    if (!standardOui || protocolId != kEtherTypeEapol) return c;

    if ((uint32_t)payloadOffset + kKeyInfoOffset + 2 > len) return c;  // not enough for EAPOL header + Key Information
    if (p[payloadOffset + 1] != kEapolTypeKey) return c;               // not an EAPOL-Key frame

    uint16_t keyInfo =
        ((uint16_t)p[payloadOffset + kKeyInfoOffset] << 8) | p[payloadOffset + kKeyInfoOffset + 1];
    bool install = (keyInfo & kKeyInfoInstall) != 0;
    bool ack = (keyInfo & kKeyInfoAck) != 0;
    bool mic = (keyInfo & kKeyInfoMic) != 0;
    bool secure = (keyInfo & kKeyInfoSecure) != 0;

    if (ack && !install) {
        c.kind = MessageKind::Message1;
    } else if (ack && install) {
        c.kind = MessageKind::Message3;
    } else if (!ack && mic && !secure) {
        c.kind = MessageKind::Message2;
    } else if (!ack && mic && secure) {
        c.kind = MessageKind::Message4;
    } else {
        c.kind = MessageKind::OtherEapolKey;
    }

    if ((uint32_t)payloadOffset + kKeyDataOffset > len) return c;  // frame ends before Key Data Length - stop here

    uint16_t keyDataLen =
        ((uint16_t)p[payloadOffset + kKeyDataLenOffset] << 8) | p[payloadOffset + kKeyDataLenOffset + 1];
    c.hasKeyData = keyDataLen > 0;

    if (c.kind == MessageKind::Message1 && keyDataLen > 0) {
        uint32_t kdStart = (uint32_t)payloadOffset + kKeyDataOffset;
        uint32_t kdEnd = kdStart + keyDataLen;
        if (kdEnd > len) kdEnd = len;  // capture may be truncated - scan only what's actually here

        // Walk Key Data as tagged elements looking for the PMKID KDE:
        // vendor-specific element (tag 0xDD) with OUI 00:0F:AC, OUI-type
        // 4 (IEEE 802.11-2020 Table 9-92 / WFA vendor convention) - only
        // its PRESENCE matters here, the 16 PMKID bytes that follow are
        // never read into anything.
        uint32_t pos = kdStart;
        while (pos + 2 <= kdEnd) {
            uint8_t tag = p[pos];
            uint8_t tlen = p[pos + 1];
            if (pos + 2 + tlen > kdEnd) break;  // malformed/truncated - stop rather than read past what we have
            if (tag == 0xDD && tlen >= 4) {
                const uint8_t* v = p + pos + 2;
                if (v[0] == 0x00 && v[1] == 0x0F && v[2] == 0xAC && v[3] == 0x04) {
                    c.hasPmkidKde = true;
                    break;
                }
            }
            pos += 2 + tlen;
        }
    }

    return c;
}

bool eapol::parseEapIdentity(const uint8_t* p, uint16_t len, String& identityOut, uint8_t supplicantMac[6]) {
    identityOut = "";

    ieee80211::ParsedDataFrame frame;
    if (!ieee80211::parseDataFrame(p, len, frame)) return false;
    if (frame.protectedFrame) return false;  // EAP is sent in the clear during 802.1X - see header

    uint8_t oui[3];
    uint16_t protocolId, payloadOffset;
    if (!ieee80211::parseSnap(p, len, frame.payloadOffset, oui, protocolId, payloadOffset)) return false;

    bool standardOui = (oui[0] == 0 && oui[1] == 0 && oui[2] == 0);
    if (!standardOui || protocolId != kEtherTypeEapol) return false;

    // EAPOL header (4 bytes): Version(1), Type(1), Length(2). We need the
    // Type byte to confirm this carries an EAP-Packet, not an EAPOL-Key.
    if ((uint32_t)payloadOffset + 4 > len) return false;
    if (p[payloadOffset + 1] != kEapolTypeEapPacket) return false;

    // EAP packet (relative to payloadOffset+4): Code(1), Identifier(1),
    // Length(2), Type(1), Type-Data(...). Need at least through the EAP
    // Type byte (payloadOffset + 4 + 5 = payloadOffset + 9).
    uint32_t eap = (uint32_t)payloadOffset + 4;
    if (eap + 5 > len) return false;
    if (p[eap] != kEapCodeResponse) return false;  // only the client's Response/Identity, not the AP's Request
    if (p[eap + 4] != kEapTypeIdentity) return false;

    uint16_t eapLen = ((uint16_t)p[eap + 2] << 8) | p[eap + 3];
    if (eapLen < 5) return false;  // header(4) + Type(1) with no room for an identity
    uint16_t idLen = eapLen - 5;
    if (idLen == 0) return false;
    if (idLen > kMaxIdentityLen) idLen = kMaxIdentityLen;

    uint32_t idStart = eap + 5;
    if (idStart + idLen > len) {
        // Capture truncated before the full identity - keep whatever
        // bytes are actually present rather than rejecting outright.
        if (idStart >= len) return false;
        idLen = (uint16_t)(len - idStart);
    }

    String id;
    for (uint16_t i = 0; i < idLen; i++) {
        char ch = (char)p[idStart + i];
        if (ch >= 0x20 && ch < 0x7F) id += ch;  // printable ASCII only - drops NULs/control bytes
    }
    if (id.isEmpty()) return false;

    identityOut = id;
    memcpy(supplicantMac, frame.srcMac, 6);
    return true;
}
