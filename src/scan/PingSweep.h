#pragma once

#include <IPAddress.h>
#include <cstdint>

// Host-aliveness probe used by the discovery sweep. See PingSweep.cpp
// for why this is a TCP connect-scan rather than genuine ICMP ping.
namespace PingSweep {

// Tries a short, fixed list of very common TCP ports with timeoutMs
// each, returning true the moment any of them accepts a connection.
// Not trying to enumerate services here — that's the port scanner
// (phase 3); this only needs to catch a live host.
bool probe(const IPAddress& ip, uint16_t timeoutMs);

}  // namespace PingSweep
