#pragma once

#include <FS.h>
#include <cstdint>

// Shared pcap-savefile writer — factored out of DeauthManager.cpp so
// PmkidManager (Fase 18) doesn't duplicate the byte layout. Deliberately
// the lowest-risk part of the "capture raw 802.11 frames" family of
// features here: the pcap format itself is small, fixed, and
// unambiguous — the actual uncertainty in this project is in the
// capture/parsing side (see ArpSpoofManager.h's RISK block), not in
// this file.
namespace pcap {

// LINKTYPE_IEEE802_11 = 105: raw 802.11 frames go in exactly as
// captured, undecoded — downstream tools (Wireshark/aircrack-ng/
// hashcat) do the actual 802.11/EAPOL parsing this firmware
// deliberately never attempts itself.
void writeGlobalHeader(File& f);

void writeRecord(File& f, const uint8_t* data, uint16_t capturedLen, uint16_t originalLen);

}  // namespace pcap
