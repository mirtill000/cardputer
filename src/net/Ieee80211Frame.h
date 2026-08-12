#pragma once

#include <cstddef>
#include <cstdint>

// Shared 802.11 Data-frame parsing — factored out of ArpSpoofManager
// after Fase 16 found two real bugs in this exact area on a real
// build, to avoid re-deriving/duplicating the trickiest, least-
// verified parsing in this project as more modules need it
// (CdpLldpSniffer, PmkidManager, RogueDhcpDetector all use this now) —
// same "verify the hard part once, in one place" principle as
// net/DnsWire.h. See ArpSpoofManager.h's RISK block for the full
// context on why this parsing can't be tested against real captured
// traffic before a real build, and fails closed (bounds-checked,
// returns false on anything unexpected) for exactly that reason.
namespace ieee80211 {

struct ParsedDataFrame {
    uint8_t srcMac[6] = {0};
    bool protectedFrame = false;
    uint16_t payloadOffset = 0;  // where the LLC/SNAP header (or raw payload) starts
};

// Parses the 802.11 MAC header of a frame captured via esp_wifi's
// promiscuous callback. Returns false for anything that isn't a plain
// (non-4-address) Data frame — management/control frames and 4-address
// WDS frames are deliberately not handled. Resolves srcMac per the
// ToDS/FromDS addressing rules (802.11-2020 §9.2.4.1.3).
bool parseDataFrame(const uint8_t* p, uint16_t len, ParsedDataFrame& out);

// Given a successfully-parsed frame's payloadOffset, checks for
// standard 802.2 LLC/SNAP encapsulation (AA AA 03 <3-byte OUI> <2-byte
// protocol ID>) — the wrapper 802.11 uses to carry an Ethernet II
// frame's payload (RFC 1042 bridging convention), for ANY original
// ethertype, not just IP: e.g. LLDP (ethertype 0x88CC) arrives with
// OUI 00:00:00, same as IPv4/ARP would, while CDP uses a Cisco-specific
// OUI (00:00:0C) with its own protocol ID in place of a real ethertype.
// Returns false if this isn't standard SNAP, or protectedFrame was
// true (encrypted content — never call this on one; there's nothing
// readable past the MAC header without the session key).
bool parseSnap(const uint8_t* p, uint16_t len, uint16_t offset, uint8_t oui[3], uint16_t& protocolId,
               uint16_t& payloadOffset);

}  // namespace ieee80211
