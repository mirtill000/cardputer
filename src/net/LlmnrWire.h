#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <cstdint>
#include <vector>

// LLMNR (RFC 4795) wire-format helpers. LLMNR reuses the DNS message
// format verbatim (RFC 1035 §4.1 header + question layout) over
// multicast UDP 224.0.0.252:5355 instead of unicast/53 - see DnsWire.h
// for the shared name-decoding helpers this builds on. Only the two
// operations scan/NameSpoofManager actually needs are here: recognize
// an incoming A-record query, and answer it claiming a given IPv4.
namespace llmnrwire {

// Parses a received LLMNR packet, filling `id`/`name`/`qtype` on
// success. Returns false if it's not a well-formed single-question
// query (QR bit set = it's a response, not a query; QDCOUNT != 1;
// truncated buffer). Does not filter by qtype - callers decide which
// types they're willing to answer (this module only ever builds an A
// response, so callers should skip anything else).
bool parseQuery(const uint8_t* buf, size_t len, uint16_t& id, String& name, uint16_t& qtype);

// Builds an A-record response claiming `answerIp` for the name in
// `queryBuf` (a buffer that already passed parseQuery). Reuses the
// query's own header+question bytes verbatim (same id, same question
// section) rather than re-encoding the name, and appends one answer RR
// pointing at the question name via a DNS compression pointer (offset
// 12, where the question name always starts in a single-question
// query). Returns an empty vector if `queryBuf`/`queryLen` don't look
// like what parseQuery just validated.
std::vector<uint8_t> buildResponse(const uint8_t* queryBuf, size_t queryLen, const IPAddress& answerIp);

}  // namespace llmnrwire
