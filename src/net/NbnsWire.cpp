#include "NbnsWire.h"

namespace {
// RFC 1001 §14.1 first-level decoding: each of the 16 raw name bytes
// was split into two nibbles, each nibble added to 'A' to keep the
// result printable-ASCII. Reverses that, then drops the trailing
// suffix/service byte (16th) and any 0x20 space padding before it -
// callers only need the human-readable name to answer the query, not
// the service-type byte.
String decodeNbnsName(const uint8_t* buf, size_t len, size_t pos) {
    if (pos + 34 > len) return "";       // 1 length byte + 32 encoded + 1 terminator
    if (buf[pos] != 0x20) return "";     // must be the standard 32-byte first-level encoding
    if (buf[pos + 33] != 0x00) return "";  // terminator

    uint8_t raw[16];
    for (int i = 0; i < 16; i++) {
        uint8_t hi = buf[pos + 1 + i * 2];
        uint8_t lo = buf[pos + 1 + i * 2 + 1];
        if (hi < 'A' || hi > 'P' || lo < 'A' || lo > 'P') return "";
        raw[i] = (uint8_t)(((hi - 'A') << 4) | (lo - 'A'));
    }

    int nameLen = 15;  // byte 16 is the suffix/service byte, never part of the displayed name
    while (nameLen > 0 && raw[nameLen - 1] == ' ') nameLen--;

    String out;
    for (int i = 0; i < nameLen; i++) out += (char)raw[i];
    return out;
}
}  // namespace

bool nbnswire::parseQuery(const uint8_t* buf, size_t len, uint16_t& txnId, String& name) {
    if (!buf || len < 12) return false;

    uint8_t flagsHi = buf[2];
    bool isResponse = (flagsHi & 0x80) != 0;       // R bit
    uint8_t opcode = (flagsHi >> 3) & 0x0F;
    if (isResponse || opcode != 0) return false;   // only answer plain name queries

    uint16_t qdcount = ((uint16_t)buf[4] << 8) | buf[5];
    if (qdcount != 1) return false;

    name = decodeNbnsName(buf, len, 12);
    if (name.isEmpty()) return false;

    size_t qtypeOff = 12 + 34;
    if (qtypeOff + 4 > len) return false;
    uint16_t qtype = ((uint16_t)buf[qtypeOff] << 8) | buf[qtypeOff + 1];
    if (qtype != 0x0020) return false;  // NB (general name query) - the only type worth answering here

    txnId = ((uint16_t)buf[0] << 8) | buf[1];
    return true;
}

std::vector<uint8_t> nbnswire::buildResponse(const uint8_t* queryBuf, size_t queryLen, const IPAddress& answerIp) {
    std::vector<uint8_t> out;
    if (!queryBuf || queryLen < 12 + 34 + 4) return out;

    out.push_back(queryBuf[0]);  // txn id, copied verbatim
    out.push_back(queryBuf[1]);
    out.push_back(0x85);  // flags: R=1 (response), AA=1, opcode=0
    out.push_back(0x00);
    out.push_back(0x00);  // QDCOUNT = 0
    out.push_back(0x00);
    out.push_back(0x00);  // ANCOUNT = 1
    out.push_back(0x01);
    out.push_back(0x00);  // NSCOUNT = 0
    out.push_back(0x00);
    out.push_back(0x00);  // ARCOUNT = 0
    out.push_back(0x00);

    // RR_NAME: the same 34-byte encoded name from the query, copied
    // verbatim (see NbnsWire.h - no compression pointer here).
    out.insert(out.end(), queryBuf + 12, queryBuf + 12 + 34);

    out.push_back(0x00);  // RR_TYPE = NB
    out.push_back(0x20);
    out.push_back(0x00);  // RR_CLASS = IN
    out.push_back(0x01);
    out.push_back(0x00);  // TTL = 300s - short on purpose, this is a live poison, not a real record
    out.push_back(0x00);
    out.push_back(0x01);
    out.push_back(0x2C);
    out.push_back(0x00);  // RDLENGTH = 6
    out.push_back(0x06);
    out.push_back(0x00);  // NB_FLAGS = 0 (unique, B-node)
    out.push_back(0x00);
    out.push_back(answerIp[0]);
    out.push_back(answerIp[1]);
    out.push_back(answerIp[2]);
    out.push_back(answerIp[3]);
    return out;
}
