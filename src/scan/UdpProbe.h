#pragma once

#include <IPAddress.h>
#include <vector>
#include "../core/Types.h"

// Lightweight, best-effort UDP service probes for the port scanner.
// Unlike the TCP scanner, a UDP port that doesn't answer is NOT
// evidence it's closed - a silently-dropped or firewalled UDP packet
// looks identical to "nothing here" without raw ICMP access (same
// honesty caveat as the TCP scanner's open-vs-rest donut - see README).
// So these probes only ever report positive findings: a service that
// actually replies to a valid, protocol-shaped request is confirmed
// open; anything else is simply left off the results list, never
// flagged "closed".
namespace UdpProbe {

// Probes DNS/53, NTP/123 and SNMP/161 on `ip` (the three most common
// UDP services worth checking on a typical home/office LAN) and appends
// a PortResult (isUdp=true) for each one that answered within
// timeoutMs.
void probeCommonServices(const IPAddress& ip, uint16_t timeoutMs, std::vector<PortResult>& results);

}  // namespace UdpProbe
