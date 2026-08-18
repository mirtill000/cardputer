#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <cstdint>
#include <vector>

// NetBIOS Name Service (RFC 1002 §4.2) wire-format helpers - the UDP/137
// broadcast name-resolution protocol Windows falls back to when DNS
// doesn't answer, same tier as LLMNR (see LlmnrWire.h) but its own
// distinct format: a 34-byte "first-level encoded" name (RFC 1001
// §14.1 - each of the 16 raw NetBIOS-name bytes split into two nibbles,
// each nibble mapped to 'A'..'P') rather than DNS-style length-prefixed
// labels.
namespace nbnswire {

// Parses a received NBT-NS packet, filling `txnId`/`name` (the decoded,
// space-trimmed NetBIOS name - not including the trailing 16th
// suffix/service byte, which callers don't need to answer a query).
// Returns false if it's not a well-formed name query (R bit set = a
// response; OPCODE != 0/query; QDCOUNT != 1; truncated buffer; the
// 0x20 length byte that must prefix a first-level-encoded name is
// missing).
bool parseQuery(const uint8_t* buf, size_t len, uint16_t& txnId, String& name);

// Builds a positive Name Query Response claiming `answerIp` owns the
// name in `queryBuf` (a buffer that already passed parseQuery). Reuses
// the query's own transaction id and encoded-name bytes verbatim
// (NBT-NS name compression is unreliable across implementations, so
// this never attempts it - unlike LlmnrWire's DNS-style pointer).
// Returns an empty vector if `queryBuf`/`queryLen` don't look like what
// parseQuery just validated.
std::vector<uint8_t> buildResponse(const uint8_t* queryBuf, size_t queryLen, const IPAddress& answerIp);

}  // namespace nbnswire
