#pragma once

#include <IPAddress.h>
#include <cstdint>

// Small helpers for IPv4 address arithmetic.
//
// Deliberately NOT using `(uint32_t)someIPAddress` for arithmetic:
// Arduino-ESP32's IPAddress stores its 4 octets in normal
// dotted-notation order (first octet first) in a byte array, and its
// operator uint32_t() just reinterprets those bytes as a native
// (little-endian, on this MCU) integer — so the *first* octet ends up
// in the integer's least-significant byte. A naive `ip + 1` on that
// value increments the first octet (192.168.1.1 -> 193.168.1.1), not
// the last one you'd expect from "next host". These helpers instead go
// through IPAddress's octet accessors and build a proper big-endian
// numeric value, so arithmetic behaves the way IPv4 addressing
// actually works. (Verified against a set of known subnet/offset cases
// before being used anywhere network-facing — see git history.)
namespace iputil {

inline uint32_t toBigEndianValue(const IPAddress& ip) {
    return (static_cast<uint32_t>(ip[0]) << 24) |
           (static_cast<uint32_t>(ip[1]) << 16) |
           (static_cast<uint32_t>(ip[2]) << 8) |
           static_cast<uint32_t>(ip[3]);
}

inline IPAddress fromBigEndianValue(uint32_t v) {
    return IPAddress((v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

inline IPAddress addOffset(const IPAddress& base, uint32_t offset) {
    return fromBigEndianValue(toBigEndianValue(base) + offset);
}

inline uint8_t popcount32(uint32_t v) {
    uint8_t n = 0;
    while (v) {
        n += (v & 1);
        v >>= 1;
    }
    return n;
}

}  // namespace iputil
