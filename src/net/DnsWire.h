#pragma once

#include <Arduino.h>
#include <cstdint>
#include <vector>

// Shared DNS wire-format helpers (RFC 1035 §4.1), used by both the
// UDP/53 DNS probe (UdpProbe.cpp) and the mDNS reverse-PTR resolver
// (MdnsReverseResolver.cpp) - centralized so the one genuinely tricky
// part (name compression) is verified once, not re-derived per caller.
// Verified against a throwaway Python reference (round-trip encode/
// decode, including a compression-pointer case) before being used here.
namespace dnswire {

// Builds a minimal standard DNS query: 12-byte header (arbitrary fixed
// txn id, recursion-desired flag, qdcount=1) + one question for `name`
// (dotted form, e.g. "10.1.168.192.in-addr.arpa") of the given qtype,
// class IN.
std::vector<uint8_t> buildQuery(const String& name, uint16_t qtype);

// Skips a (possibly-compressed) name starting at buf[pos] during a
// single linear pass through the packet (header -> question -> answer
// NAME fields), returning the offset just past it. Does NOT follow a
// compression pointer's target - it only needs to know how many bytes
// the pointer itself (always exactly 2) or the inline label sequence
// occupies at THIS position, per RFC 1035 §4.1.4. Returns -1 if the
// buffer runs out first.
int skipName(const uint8_t* buf, int len, int pos);

// Fully decodes a (possibly-compressed) name's dotted-string value,
// following compression pointers as needed (bounded to a small number
// of jumps, so a malformed/malicious pointer cycle can't loop forever).
// Used for RDATA fields (e.g. a PTR record's target), where the actual
// text matters, not just how many bytes it occupied inline. Returns ""
// on any malformed/truncated input.
String decodeName(const uint8_t* buf, int len, int pos);

}  // namespace dnswire
