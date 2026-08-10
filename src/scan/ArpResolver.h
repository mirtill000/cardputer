#pragma once

#include <IPAddress.h>
#include <cstdint>

// Reads the ESP32's lwIP ARP cache for a given IP — this is what turns
// "this host answered a probe" into a MAC address, which is what the
// OUI vendor lookup and device classifier actually key off of.
//
// This is the single most platform/version-sensitive file in the
// codebase: it reaches past Arduino's WiFi wrapper into the underlying
// esp_netif/lwIP layer, because Arduino-ESP32 doesn't expose ARP table
// access itself. If a future arduino-esp32 core release changes
// WiFiSTAClass::netif() or esp_netif_get_netif_impl()'s signature, this
// is the only file that needs to change — everything else in scan/
// talks to it only through lookupMac() below.
namespace ArpResolver {

// Returns true and fills mac[6] if the ARP cache already has an entry
// for ip. Does NOT send a new ARP request itself — call this only after
// something (a ping, a TCP connect attempt) has already caused lwIP to
// resolve the address as a side effect of sending a packet to it, which
// is how ScanManager uses it (see ScanManager::probeHost).
bool lookupMac(const IPAddress& ip, uint8_t mac[6]);

}  // namespace ArpResolver
