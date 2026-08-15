#include "EapolWire.h"
#include "Ieee80211Frame.h"

namespace {
constexpr uint8_t kEapolTypeKey = 3;        // EAPOL Type byte: 0=EAP-Packet, 1=Start, 2=Logoff, 3=Key, 5=ASF-Alert
constexpr uint16_t kEtherTypeEapol = 0x888E;

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
