#pragma once

#include <cstddef>
#include <cstdint>
#include <esp_wifi_types.h>

// Shared promiscuous-mode frame dispatcher.
//
// Two problems this replaces:
//
//  1. esp_wifi accepts exactly ONE promiscuous RX callback. Every passive
//     sniffer used to install its own, so starting a second one silently
//     stole the radio from the first (the "RF:N!" conflict ActivityStatus
//     warns about), and stopping either one turned promiscuous mode off
//     under the other.
//  2. That callback runs on the WiFi driver's own task. Several sniffers
//     did their real work inline there - taking a mutex with a 200ms
//     timeout, push_back into a vector, building Strings to notify() the
//     UI - which can stall the driver (dropped frames, beacon timeouts,
//     disconnects, task-watchdog trips). SentinelManager had already
//     moved to "copy into a queue, process on our own task"; this makes
//     that the only way frames are handled.
//
// So: one callback, installed here, that only copies the frame into a
// FreeRTOS queue (no lock, no allocation). One dispatcher task drains the
// queue and calls every subscriber whose type mask matches - on that
// task, where blocking on a mutex or allocating is fine. Promiscuous mode
// is reference-counted: on with the first subscriber, off with the last.
//
// Managers that need exclusive control of the radio (they hop channels,
// inject frames or keep their own capture queue: deauth/PMKID capture,
// ARP spoof, SENTINEL) still install their own callback; running one of
// those alongside a subscriber remains a real conflict and is still
// flagged by ActivityStatus.
namespace promisc {

// Frames longer than this are truncated; the subscriber still sees the
// on-air length in origLen. Every current consumer parses headers / short
// payloads (DHCP, CDP/LLDP TLVs, EAPOL, beacon IEs) well inside this.
constexpr uint16_t kSnapLen = 1024;

struct Frame {
    uint16_t len;      // bytes valid in data (<= kSnapLen)
    uint16_t origLen;  // length reported by the radio (rx_ctrl.sig_len)
    int8_t rssi;
    uint8_t channel;   // channel the frame was received on
    uint8_t type;      // wifi_promiscuous_pkt_type_t
    uint8_t data[kSnapLen];
};

// Called on the dispatcher task. May block briefly (mutex) - but every
// subscriber shares the one task, so keep it short.
using Handler = void (*)(const Frame& frame);

constexpr uint32_t kMgmt = 1u << WIFI_PKT_MGMT;
constexpr uint32_t kData = 1u << WIFI_PKT_DATA;

// Creates the lock. Call once from setup(), before any subscribe().
void begin();

// Returns a subscription id >= 0, or -1 if the dispatcher could not be
// started (out of memory) or every slot is taken. The first subscription
// lazily creates the frame queue + task and enables promiscuous mode.
int subscribe(uint32_t typeMask, Handler handler);

// Ends a subscription; the last one turns promiscuous mode off. A frame
// already dequeued when this is called may still be delivered once.
void unsubscribe(int id);

size_t subscriberCount();
uint32_t droppedFrames();  // frames lost because the queue was full

}  // namespace promisc
