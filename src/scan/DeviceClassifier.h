#pragma once

#include "../core/Types.h"

// Best-effort heuristic device classification. Deliberately conservative:
// OUI vendor strings are a weak signal (the same manufacturer OUI block
// is reused across wildly different product lines — e.g. a "Google"
// device could be a Chromecast or a Nest speaker; an "ASUSTeK COMPUTER
// INC." OUI shows up on both routers and laptops), so classify() only
// commits to a category on a reasonably specific keyword match and
// otherwise leaves the host Unknown rather than guessing. Once the port
// scanner (phase 3) adds open-port evidence, classify() is meant to be
// called again with that signal folded in — see the TODO in
// scan/ScanManager.cpp.
namespace DeviceClassifier {

// isGateway: true if this host's IP is the network's default gateway —
// by far the strongest signal available (basically always a router),
// so it short-circuits everything else.
void classify(HostInfo& host, bool isGateway);

}  // namespace DeviceClassifier
