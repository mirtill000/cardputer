#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <cstdint>

// Best-effort hostname resolution via multicast DNS reverse (PTR)
// lookup - the fallback HostnameResolver (NBNS) doesn't cover: phones,
// Macs, Chromecasts and most consumer IoT gear answer mDNS, not NBNS.
//
// This is NOT what arduino-esp32's ESPmDNS library does: that library
// advertises *this* device's own name and browses service types, it
// has no API for an arbitrary one-shot reverse query against someone
// else's IP. This hand-rolls exactly that one query (RFC 6762 permits
// PTR queries against the standard reverse-mapping zones - e.g.
// 168.192.in-addr.arpa for RFC1918 192.168.0.0/16 - over mDNS without
// needing a ".local" suffix), reusing the shared DNS wire-format code
// in net/DnsWire.h that HostnameResolver's NBNS sibling does NOT need
// (NBNS has no compression) but a real DNS/mDNS response does.
//
// Higher hardware-verification risk than the rest of this codebase's
// networking code: multicast UDP (joining 224.0.0.251, binding port
// 5353) isn't exercised anywhere else here to cross-check against, and
// none of this has been compiled or run (see README's testing note).
namespace MdnsReverseResolver {

// Returns "" if no PTR reply arrives within timeoutMs, or the reply
// doesn't parse as expected - a device that doesn't speak mDNS, one
// that doesn't answer reverse queries, a firewalled port, or a
// malformed reply are all treated the same: absence of evidence, not
// evidence of absence (same convention as HostnameResolver::resolve()).
// On success, returns just the first label of the PTR target (e.g.
// "mydevice" out of "mydevice.local"), matching the short-name style
// HostnameResolver already returns.
String resolve(const IPAddress& ip, uint16_t timeoutMs);

}  // namespace MdnsReverseResolver
