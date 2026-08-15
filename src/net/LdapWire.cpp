#include "LdapWire.h"
#include <cstring>

namespace {

// --- The two request PDUs this module ever sends, as fixed byte
// sequences (both are always exactly the same bytes — no field this
// firmware fills in ever varies) — verified byte-for-byte against
// Python's pyasn1 + ldap3.protocol.rfc4511 (a real, standards-compliant
// LDAP ASN.1 module) before being pasted in here, same "hand the encoder
// nothing to get subtly wrong" reasoning as DeauthManager's fixed
// deauth-frame byte array. See LdapWire.h for what each one contains.

// LDAPMessage { messageID=1, protocolOp=BindRequest{version=3, name="",
// authentication=simple("")} }
constexpr uint8_t kAnonymousBind[] = {
    0x30, 0x0c, 0x02, 0x01, 0x01, 0x60, 0x07, 0x02, 0x01, 0x03, 0x04, 0x00,
    0x80, 0x00,
};  // 14 bytes

// LDAPMessage { messageID=2, protocolOp=SearchRequest{baseObject="",
// scope=baseObject(0), derefAliases=never(0), sizeLimit=0, timeLimit=5,
// typesOnly=FALSE, filter=present("objectClass"),
// attributes=["namingContexts","defaultNamingContext","dnsHostName"]} }
constexpr uint8_t kRootDseSearch[] = {
    0x30, 0x58, 0x02, 0x01, 0x02, 0x63, 0x53, 0x04, 0x00, 0x0a, 0x01, 0x00,
    0x0a, 0x01, 0x00, 0x02, 0x01, 0x00, 0x02, 0x01, 0x05, 0x01, 0x01, 0x00,
    0x87, 0x0b, 0x6f, 0x62, 0x6a, 0x65, 0x63, 0x74, 0x43, 0x6c, 0x61, 0x73,
    0x73, 0x30, 0x33, 0x04, 0x0e, 0x6e, 0x61, 0x6d, 0x69, 0x6e, 0x67, 0x43,
    0x6f, 0x6e, 0x74, 0x65, 0x78, 0x74, 0x73, 0x04, 0x14, 0x64, 0x65, 0x66,
    0x61, 0x75, 0x6c, 0x74, 0x4e, 0x61, 0x6d, 0x69, 0x6e, 0x67, 0x43, 0x6f,
    0x6e, 0x74, 0x65, 0x78, 0x74, 0x04, 0x0b, 0x64, 0x6e, 0x73, 0x48, 0x6f,
    0x73, 0x74, 0x4e, 0x61, 0x6d, 0x65,
};  // 90 bytes

// --- Minimal BER TLV reader, used for everything this module parses. ---

struct BerTlv {
    uint8_t tag = 0;
    size_t valueOffset = 0;
    size_t valueLen = 0;
    size_t totalLen = 0;  // header + value, i.e. how many bytes this TLV occupies from `pos`
};

// Reads one tag+length(+implicit value) starting at buf[pos]. Handles
// both BER short-form length (single byte, value < 128) and long-form
// (high bit set, low 7 bits = how many big-endian length bytes follow) —
// real LDAP responses use long-form for anything over 127 bytes (e.g. a
// rootDSE entry with several naming contexts), so both forms are needed
// for correctness, not just short-form for simplicity. Indefinite-length
// (0x80) and length encodings needing more than 4 bytes are rejected —
// neither can happen in anything a real LDAP server would send back for
// the small, bounded queries this module makes. Fails closed (returns
// false) on anything truncated or malformed rather than guessing, same
// convention as every other hand-rolled parser in this codebase.
bool berReadTlv(const uint8_t* buf, size_t len, size_t pos, BerTlv& out) {
    if (pos >= len) return false;
    uint8_t tag = buf[pos];
    size_t lenPos = pos + 1;
    if (lenPos >= len) return false;

    uint8_t lenByte = buf[lenPos];
    size_t valueOff, valueLen, headerLen;
    if ((lenByte & 0x80) == 0) {
        valueLen = lenByte;
        valueOff = lenPos + 1;
        headerLen = 2;
    } else {
        uint8_t nBytes = lenByte & 0x7F;
        if (nBytes == 0 || nBytes > 4) return false;  // indefinite-length or unreasonably large
        if (lenPos + 1 + nBytes > len) return false;
        size_t l = 0;
        for (uint8_t i = 0; i < nBytes; i++) l = (l << 8) | buf[lenPos + 1 + i];
        valueLen = l;
        valueOff = lenPos + 1 + nBytes;
        headerLen = (size_t)1 + 1 + nBytes;
    }
    if (valueOff + valueLen > len) return false;  // truncated/malformed - fail closed

    out.tag = tag;
    out.valueOffset = valueOff;
    out.valueLen = valueLen;
    out.totalLen = headerLen + valueLen;
    return true;
}

// Walks past the outer LDAPMessage SEQUENCE and its messageID INTEGER,
// landing on the protocolOp TLV. Returns false if the shape doesn't
// match (not a SEQUENCE, or messageID isn't an INTEGER where expected).
bool findProtocolOp(const uint8_t* buf, size_t len, BerTlv& op) {
    BerTlv outer;
    if (!berReadTlv(buf, len, 0, outer) || outer.tag != 0x30) return false;

    BerTlv msgId;
    if (!berReadTlv(buf, len, outer.valueOffset, msgId) || msgId.tag != 0x02) return false;

    size_t opPos = outer.valueOffset + msgId.totalLen;
    return berReadTlv(buf, len, opPos, op);
}

// Shared by parseBindResponse (op tag 0x61) and isSearchResultDone (op
// tag 0x65) - both are an LDAPResult SEQUENCE whose first element is the
// resultCode ENUMERATED (RFC 4511 §4.1.9), at a fixed relative position.
bool readResultCode(const uint8_t* buf, size_t len, const BerTlv& op, uint8_t& resultCode) {
    BerTlv rc;
    if (!berReadTlv(buf, len, op.valueOffset, rc) || rc.tag != 0x0A || rc.valueLen < 1) return false;
    resultCode = buf[rc.valueOffset];
    return true;
}

}  // namespace

std::vector<uint8_t> ldapwire::buildAnonymousBind() {
    return std::vector<uint8_t>(kAnonymousBind, kAnonymousBind + sizeof(kAnonymousBind));
}

std::vector<uint8_t> ldapwire::buildRootDseSearch() {
    return std::vector<uint8_t>(kRootDseSearch, kRootDseSearch + sizeof(kRootDseSearch));
}

bool ldapwire::parseBindResponse(const uint8_t* buf, size_t len, bool& success) {
    BerTlv op;
    if (!findProtocolOp(buf, len, op) || op.tag != 0x61) return false;  // 0x61 = [APPLICATION 1] BindResponse

    uint8_t resultCode;
    if (!readResultCode(buf, len, op, resultCode)) return false;
    success = (resultCode == 0);  // 0 = success (RFC 4511 §4.1.9)
    return true;
}

bool ldapwire::isSearchResultDone(const uint8_t* buf, size_t len) {
    BerTlv op;
    if (!findProtocolOp(buf, len, op)) return false;
    return op.tag == 0x65;  // [APPLICATION 5] SearchResultDone - resultCode not checked, presence is the signal
}

bool ldapwire::parseSearchResultEntry(const uint8_t* buf, size_t len, const std::vector<String>& wantedAttrs,
                                       std::vector<String>& out) {
    BerTlv op;
    if (!findProtocolOp(buf, len, op) || op.tag != 0x64) return false;  // 0x64 = [APPLICATION 4] SearchResultEntry

    // SearchResultEntry ::= SEQUENCE { objectName LDAPDN, attributes
    // PartialAttributeList }. objectName is skipped (not needed - this
    // module always queries a single, known base object).
    BerTlv objectName;
    if (!berReadTlv(buf, len, op.valueOffset, objectName) || objectName.tag != 0x04) return false;

    size_t attrsPos = op.valueOffset + objectName.totalLen;
    BerTlv attrsSeq;
    if (!berReadTlv(buf, len, attrsPos, attrsSeq) || attrsSeq.tag != 0x30) return false;

    size_t p = attrsSeq.valueOffset;
    size_t end = attrsSeq.valueOffset + attrsSeq.valueLen;
    while (p < end) {
        // PartialAttribute ::= SEQUENCE { type AttributeDescription,
        // vals SET OF AttributeValue } - a fixed two-element SEQUENCE,
        // walked positionally rather than by re-validating each tag,
        // same style CdpLldpSniffer's TLV walks already use.
        BerTlv pa;
        if (!berReadTlv(buf, len, p, pa) || pa.tag != 0x30) break;

        BerTlv typeTlv;
        if (!berReadTlv(buf, len, pa.valueOffset, typeTlv) || typeTlv.tag != 0x04) break;

        String attrName;
        for (size_t i = 0; i < typeTlv.valueLen; i++) attrName += (char)buf[typeTlv.valueOffset + i];

        // Only decode the value if this is one of the attributes the
        // caller asked for - every other PartialAttribute in the entry
        // is skipped entirely (rootDSE can carry dozens; this module
        // only ever wants three).
        for (size_t w = 0; w < wantedAttrs.size(); w++) {
            if (wantedAttrs[w] != attrName) continue;
            if (w >= out.size()) break;  // caller pre-sizes `out` to match wantedAttrs - defensive, not expected to trip

            size_t valsPos = pa.valueOffset + typeTlv.totalLen;
            BerTlv valsSet;
            // 0x31 = SET OF (universal tag 17, constructed) - the vals
            // container. Only the FIRST value in the SET is kept (see
            // LdapWire.h) - a real rootDSE's namingContexts is often
            // multi-valued (one per naming context on the DC), and this
            // is a one-line summary, not an exhaustive dump.
            if (berReadTlv(buf, len, valsPos, valsSet) && valsSet.tag == 0x31 && valsSet.valueLen > 0) {
                BerTlv firstVal;
                if (berReadTlv(buf, len, valsSet.valueOffset, firstVal) && firstVal.tag == 0x04) {
                    String v;
                    for (size_t i = 0; i < firstVal.valueLen; i++) v += (char)buf[firstVal.valueOffset + i];
                    out[w] = v;
                }
            }
            break;
        }

        p += pa.totalLen;
    }

    return true;
}
