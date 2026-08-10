#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <cstdint>

// Best-effort hostname resolution via NBNS (NetBIOS Name Service,
// UDP/137 "Node Status" query). See HostnameResolver.cpp for why NBNS
// and not mDNS.
namespace HostnameResolver {

// Returns "" if no name could be resolved within timeoutMs — a
// non-Windows/non-Samba device, a firewalled port, or just no reply in
// time are all treated the same: absence of evidence, not evidence of
// absence.
String resolve(const IPAddress& ip, uint16_t timeoutMs);

}  // namespace HostnameResolver
