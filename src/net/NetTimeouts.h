#pragma once

#include <cstdint>

// One home for the TCP connect/read timeout policy the scan modules use.
// Before this, seven modules each defined their own private
// kConnectTimeoutMs/kReadTimeoutMs with five different values scattered
// across headers and .cpp files — no way to see the policy, or the fact
// that three of them share one tier, from any single place. Each module
// still keeps its own local kConnectTimeoutMs/kReadTimeoutMs name (so its
// call sites are unchanged) but now defines it as one of these named
// tiers, so the actual numbers — and which modules share a tier — live
// here and change here.
//
// These are per-tier on purpose: they are NOT one global value. A quick
// "is this port even there" probe and a multi-message auth handshake
// legitimately want different budgets on a device this slow; the tiers
// name that intent instead of hiding it behind one number or seven.
namespace nettimeout {

// Lightweight single-shot probes: connect, send one request, read one
// reply from a service that's either present or silent. Shared by
// DATASTORE SWEEP, LDAP SWEEP and IOT/OT SWEEP. Short so a dead port
// doesn't stall the whole sweep.
constexpr uint16_t kQuickProbeMs = 700;

// HTTP discovery round trips (NTLM-over-HTTP disclosure): a real web
// server handshake, a touch more headroom than a bare TCP probe.
constexpr uint16_t kHttpDiscoveryMs = 800;

// Interactive authentication handshakes that exchange several messages
// before a verdict (SERVICE AUDIT: FTP/SMB/Redis/MySQL/PostgreSQL/VNC/
// HTTP login attempts). Needs to tolerate a slow multi-step exchange.
constexpr uint16_t kInteractiveAuthMs = 2500;

// SMB NEGOTIATE: the most generous — some SMB stacks are sluggish
// answering the very first PDU on a cold connection.
constexpr uint16_t kSmbMs = 3000;

}  // namespace nettimeout
