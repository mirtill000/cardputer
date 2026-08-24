#pragma once

#include "../core/Types.h"

// Best-effort heuristic device classification. Deliberately conservative:
// OUI vendor strings are a weak signal (the same manufacturer OUI block
// is reused across wildly different product lines — e.g. a "Google"
// device could be a Chromecast or a Nest speaker; an "ASUSTeK COMPUTER
// INC." OUI shows up on both routers and laptops), so classify() only
// commits to a category on a reasonably specific keyword match and
// otherwise leaves the host Unknown rather than guessing. A second,
// independent pass over host.hostname (NBNS/mDNS/DHCP-supplied) fills
// in Unknown hosts and refines the two vendor categories whose OUI
// blocks are known to be reused across unrelated product lines (Mobile,
// Computer) — see the comment in classify() itself. Open-port evidence
// from the port scanner is not folded in yet — that scan only runs
// on-demand per-host from HOST DETAIL, well after this first pass.
namespace DeviceClassifier {

// isGateway: true if this host's IP is the network's default gateway —
// by far the strongest signal available (basically always a router),
// so it short-circuits everything else.
void classify(HostInfo& host, bool isGateway);

}  // namespace DeviceClassifier
