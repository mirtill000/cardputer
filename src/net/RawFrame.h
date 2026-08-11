#pragma once

#include <cstddef>
#include <cstdint>

// Shared raw-Ethernet-frame send, used by ArpSpoofManager to inject
// hand-built ARP/DNS reply frames onto the WiFi link. Deliberately built
// on the SAME plain-lwIP `netif_default` access ArpResolver.cpp already
// uses and has already been build-verified on real hardware for (see
// its header comment) — going through lwIP's own netif->linkoutput hook
// rather than reaching for esp_wifi_internal_tx()/other ESP-IDF
// WiFi-driver-private APIs means this depends only on plain lwIP
// (lwip/netif.h, lwip/pbuf.h), a much more stable surface across
// esp-idf versions than anything under esp_wifi's internal headers.
namespace RawFrame {

// data/len: a complete raw Ethernet II frame (6-byte dest MAC, 6-byte
// src MAC, 2-byte ethertype, payload) — the driver adds the actual
// 802.11 header itself once this reaches netif_default's linkoutput.
// Returns false if the netif isn't up yet or the send failed.
bool send(const uint8_t* data, size_t len);

}  // namespace RawFrame
