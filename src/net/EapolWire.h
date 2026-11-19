#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

// Structural classifier for captured EAPOL-Key frames (the WPA/WPA2
// 4-way handshake) — reads ONLY frame-control bits, the LLC/SNAP
// EtherType, the EAPOL header's Type byte, and the Key Information flag
// bits (Install/Ack/MIC/Secure) to work out which of the four handshake
// messages a frame structurally looks like, plus whether Message 1
// carries a PMKID (RSN vendor KDE, OUI 00:0F:AC type 4) in its Key Data.
//
// classify() DELIBERATE LIMIT: it never reads the Key Nonce, Key MIC,
// Key IV/RSC/ID, or the PMKID's own 16 bytes — only confirms a PMKID
// marker is PRESENT. The one place those PMKID bytes ARE read is the
// separate, explicit extractPmkid() below, used solely to write the
// standard hashcat `22000` export of a capture the firmware ALREADY
// saves in full to a .pcap (the PMKID is already on the SD card either
// way — the export just re-encodes it into the format hashcat ingests).
// This firmware still never attempts to derive, guess, or verify a
// passphrase from anything captured — no cracking happens on-device,
// same as PmkidManager.h/DeauthManager.h. What classify() adds: telling
// you "this capture structurally looks like it has a usable PMKID/
// handshake" on-device instead of only "N frames captured, find out on a
// PC" — nothing beyond what Wireshark's EAPOL dissector shows for free.
//
// Message-number heuristic (standard, used by Wireshark/aircrack-ng/
// hcxdumptool alike — see IEEE 802.11-2020 §12.7.2 Key Information
// field): Message1 = Ack set, Install clear. Message3 = Ack set,
// Install set. Message2 = Ack clear, MIC set, Secure clear. Message4 =
// Ack clear, MIC set, Secure set. Anything else is left unclassified
// rather than guessed.
//
// Not verified against a real captured handshake on hardware (no build
// environment here — see README's standing note on this), only against
// the publicly documented frame layout; same caveat this project always
// states for its other from-scratch 802.11 parsers (ArpSpoofManager/
// DeauthManager's own RISK blocks).
namespace eapol {

enum class MessageKind : uint8_t {
    NotEapolKey,   // not a (readable) EAPOL-Key frame at all
    Message1,
    Message2,
    Message3,
    Message4,
    OtherEapolKey  // EAPOL-Key, but the flag combination didn't match any of the four above
};

struct Classification {
    MessageKind kind = MessageKind::NotEapolKey;
    bool hasKeyData = false;   // Key Data Length > 0 (any message)
    bool hasPmkidKde = false;  // Message1 only: a PMKID KDE marker was found in Key Data
};

// p/len: a raw 802.11 frame exactly as delivered by esp_wifi's
// promiscuous callback (same shape ArpSpoofManager/CdpLldpSniffer/
// PmkidManager/DeauthManager already receive) — call this BEFORE any
// truncation for pcap storage, so a PMKID KDE appearing late in Key
// Data isn't missed. Protected (encrypted) frames are never classified
// as EAPOL — genuine EAPOL is always sent in the clear at the 802.11
// layer, so a frame marked protected here is either mis-detected or
// simply unreadable either way.
Classification classify(const uint8_t* p, uint16_t len);

// Copies the 16-byte PMKID out of a Message-1 frame's RSN PMKID KDE, for
// building a hashcat `22000` (WPA*01) line from a capture already written
// to pcap. Returns true and fills pmkidOut[16] only when a well-formed
// PMKID KDE (OUI 00:0F:AC, type 4) with its full 16 bytes is present and
// not all-zero; false otherwise (no KDE, truncated, or an all-zero PMKID,
// which is a padding placeholder, not a real one). Same bounds-checked,
// fail-closed style as classify(). Does NOT read nonce/MIC and still
// cracks nothing — see the header-top note.
bool extractPmkid(const uint8_t* p, uint16_t len, uint8_t pmkidOut[16]);

// EAP-Response/Identity extraction — a DIFFERENT EAPOL type from the
// EAPOL-Key handshake classify() above (EAPOL Type 0 = EAP-Packet, not
// Type 3 = Key). During a WPA-Enterprise (802.1X) association the very
// first EAP exchange carries the supplicant's "outer" identity in the
// CLEAR, before the PEAP/TTLS TLS tunnel is negotiated — a username
// like "user@realm", "DOMAIN\user", or an anonymous placeholder
// ("anonymous@realm") the operator chose. Reading it is the same
// category of passive over-the-air recon as EapolWire's PMKID-presence
// check or BeaconProbeSniffer's probe-request SSIDs: a value the client
// broadcasts unencrypted by the ordinary operation of 802.1X, visible
// to any receiver on the AP's channel. This is a classic, reportable
// enterprise-WiFi audit finding (a network disclosing real usernames in
// the clear before the tunnel), exactly what Wireshark's own EAP
// dissector shows for free from the same bytes.
//
// Fills `identityOut` (the cleartext identity string, non-printable
// bytes dropped) and `supplicantMac` (the client that sent it) and
// returns true ONLY for a well-formed EAP-Response/Identity with a
// non-empty identity. Everything else — EAPOL-Key frames, EAP-Request
// (the AP asking, code 1), other EAP types, encrypted frames, truncated
// buffers — returns false. Same fail-closed, bounds-checked style as
// classify(); same "never verified against a real capture on hardware"
// caveat (see header top).
bool parseEapIdentity(const uint8_t* p, uint16_t len, String& identityOut, uint8_t supplicantMac[6]);

}  // namespace eapol
