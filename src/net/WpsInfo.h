#pragma once

#include <cstdint>

// WPS / WSC (Wi-Fi Simple Config) attribute parsing + attackability
// classification, shared by the passive capture (BeaconProbeSniffer reads
// the WPS IE out of beacons AND probe responses) and the active WPS probe.
//
// This is RECON ONLY: it reads attributes the AP already advertises in the
// clear and works out whether the AP is a candidate for an offline WPS-PIN
// / Pixie-Dust attempt. It performs no PIN exchange and runs no attack -
// the firmware still never attempts a PIN against anything (see
// BeaconProbeSniffer.h / PmkidManager.h); the actual attack stays offline
// (reaver / pixiewps).
//
// Pure (no Arduino / FreeRTOS deps) so it can be unit-tested on the host.
namespace wps {

// WSC Config Methods bitmask (WSC spec Table 33). Only the PIN-bearing
// methods matter for candidacy: a push-button-only AP has no PIN to
// recover, so it is not a PIN/Pixie target.
constexpr uint16_t kCfgLabel = 0x0004;
constexpr uint16_t kCfgDisplay = 0x0008;
constexpr uint16_t kCfgKeypad = 0x0100;
inline bool supportsPin(uint16_t configMethods) {
    return (configMethods & (kCfgLabel | kCfgDisplay | kCfgKeypad)) != 0;
}

struct Info {
    bool present = false;            // a WPS IE was found on this AP
    bool locked = false;             // AP Setup Locked (0x1057) - PIN attempts are being refused
    uint16_t configMethods = 0;      // Config Methods (0x1008) raw bitmask
    uint8_t version = 0;             // Version (0x104A): 0x10=1.0, 0x20=2.0; 0 if absent
    uint16_t devicePasswordId = 0xFFFF;  // Device Password ID (0x1012); 0xFFFF = absent
    bool selectedRegistrar = false;  // Selected Registrar (0x1041) - a registrar window is open right now
};

enum class Candidacy : uint8_t {
    NotWps,          // no WPS IE
    Locked,          // WPS on but AP Setup Locked - PIN attempts refused
    PushButtonOnly,  // WPS on, unlocked, but no PIN method advertised - nothing to recover
    PinCandidate,    // WPS on, unlocked, PIN-capable - worth an offline reaver/pixiewps attempt
};

inline Candidacy candidacy(const Info& w) {
    if (!w.present) return Candidacy::NotWps;
    if (w.locked) return Candidacy::Locked;
    if (!supportsPin(w.configMethods)) return Candidacy::PushButtonOnly;
    return Candidacy::PinCandidate;
}

inline const char* candidacyLabel(Candidacy c) {
    switch (c) {
        case Candidacy::NotWps:         return "no WPS";
        case Candidacy::Locked:         return "WPS locked";
        case Candidacy::PushButtonOnly: return "WPS PBC-only";
        case Candidacy::PinCandidate:   return "WPS-PIN candidate";
    }
    return "?";
}

// Parses the WSC attribute list (the bytes AFTER the IE's 4-byte
// OUI+type header, as BeaconProbeSniffer's findWpsIe already locates).
// WSC TLVs are 2-byte BIG-ENDIAN Attribute ID + 2-byte BIG-ENDIAN Length
// + value - the opposite byte order from 802.11 tagged IEs. Fails closed
// on anything short/malformed, stopping at the first bad TLV. Does not
// set `present` (the caller sets it once findWpsIe confirms the IE).
inline Info parseAttributes(const uint8_t* p, uint16_t len) {
    Info w;
    uint16_t pos = 0;
    while ((uint32_t)pos + 4 <= len) {
        uint16_t id = ((uint16_t)p[pos] << 8) | p[pos + 1];
        uint16_t l = ((uint16_t)p[pos + 2] << 8) | p[pos + 3];
        if ((uint32_t)pos + 4 + l > len) break;  // malformed - stop rather than read past the IE
        const uint8_t* v = p + pos + 4;
        switch (id) {
            case 0x1057: if (l >= 1) w.locked = v[0] != 0; break;                       // AP Setup Locked
            case 0x1008: if (l >= 2) w.configMethods = ((uint16_t)v[0] << 8) | v[1]; break;  // Config Methods
            case 0x104A: if (l >= 1) w.version = v[0]; break;                           // Version
            case 0x1012: if (l >= 2) w.devicePasswordId = ((uint16_t)v[0] << 8) | v[1]; break;  // Device Password ID
            case 0x1041: if (l >= 1) w.selectedRegistrar = v[0] != 0; break;            // Selected Registrar
            default: break;
        }
        pos += 4 + l;
    }
    return w;
}

}  // namespace wps
