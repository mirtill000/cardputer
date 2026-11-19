#pragma once

#include <Arduino.h>
#include <cstdint>

// Builds a hashcat `22000` (hc22000) line from captured material. This is
// pure string formatting of bytes the firmware ALREADY captures and saves
// to a .pcap - it re-encodes them into the format `hashcat -m 22000`
// ingests, so the user doesn't have to run hcxpcapngtool on a PC first.
// It performs NO cracking and derives nothing: see net/EapolWire.h.
//
// Format (one line), WPA-PMKID variant 01 (the only one produced here;
// the full 4-way-handshake variant 02 needs the EAPOL frame with its MIC
// zeroed and a message-pair byte, which isn't emitted on-device):
//
//   WPA*01*<pmkid>*<ap_mac>*<client_mac>*<essid>***
//
// All fields lowercase hex, no separators inside a field; essid is the
// SSID's raw bytes hex-encoded. See the hashcat WPA-PBKDF2-PMKID+EAPOL
// mode documentation for the field layout.
namespace hc22000 {

inline String toHex(const uint8_t* data, size_t len) {
    static const char* kHex = "0123456789abcdef";
    String out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out += kHex[data[i] >> 4];
        out += kHex[data[i] & 0x0F];
    }
    return out;
}

inline String essidHex(const String& ssid) {
    return toHex(reinterpret_cast<const uint8_t*>(ssid.c_str()), ssid.length());
}

// pmkid[16], apMac[6], clientMac[6]. Returns the full line WITHOUT a
// trailing newline.
inline String pmkidLine(const uint8_t pmkid[16], const uint8_t apMac[6], const uint8_t clientMac[6],
                        const String& ssid) {
    return "WPA*01*" + toHex(pmkid, 16) + "*" + toHex(apMac, 6) + "*" + toHex(clientMac, 6) + "*" + essidHex(ssid) +
           "***";
}

}  // namespace hc22000
