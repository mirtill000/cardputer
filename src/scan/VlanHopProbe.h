#pragma once

#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <esp_wifi.h>

// "VLAN hopping" probe: 802.1Q tag-leak detector plus a best-effort
// double-tagging injection, adapted to what a WiFi-only device can
// actually do.
//
// REAL LIMITATION, not a bug, read this before trusting a negative
// result: classic VLAN hopping (switch spoofing via DTP, or double-
// tagging through a trunk's native VLAN) is an attack against a WIRED
// switch port. This is a WiFi station with no Ethernet PHY at all — it
// can only see or inject 802.1Q tags at all if the AP itself bridges
// tagged frames onto the wireless segment, which is NOT how 802.11
// normally works (VLAN tagging is a wired-side concept; a well-behaved
// AP strips tags before/after the wireless hop). So there are really
// two, much narrower, honest things this can do:
//
//  1. PASSIVE (the reliable part): watch promiscuously for any frame
//     that carries an 802.1Q tag (EtherType 0x8100) at all inside the
//     SNAP-encapsulated payload a WiFi data frame carries. Seeing ONE
//     is already a finding on its own — a client shouldn't see VLAN
//     tags on the wireless segment, ever — independent of whether a
//     double-tag hop would actually work.
//
//  2. ACTIVE (best-effort, unconfirmable): craft and send ONE
//     broadcast ARP frame carrying two stacked 802.1Q tags (outer =
//     the VLAN this device is presumably already on, inner = the
//     target VLAN), same trick a real double-tagging attack uses.
//     Whether it actually reaches the target VLAN depends entirely on
//     whether the AP's uplink switch port has a matching, permissively
//     configured native VLAN — something this device has no way to
//     observe from here. A successful send is reported as "sent", NOT
//     as "worked": only a listener already sitting on the target VLAN
//     could ever confirm that.
//
// Same shared-radio caveat as every other promiscuous feature (see
// ArpSpoofManager.h's RISK block): only OPEN networks are readable
// (WPA frames are encrypted), and only one promiscuous callback runs
// at a time.
class VlanHopProbe {
public:
    struct TagSighting {
        uint8_t mac[6] = {0};
        uint16_t outerVlanId = 0;
        bool doubleTagged = false;  // a SECOND 0x8100 tag was stacked right after the first
        uint16_t innerVlanId = 0;   // valid only if doubleTagged
        uint32_t count = 0;
        uint32_t lastSeenMs = 0;
    };

    void begin(QueueHandle_t outQueue);

    void start();  // passive tag-leak listen, no-op if already running
    void stop();
    bool isRunning() const { return _running; }

    // One-shot active probe: builds and sends a single broadcast ARP
    // request wrapped in two stacked 802.1Q tags (outer=nativeVlanId,
    // inner=targetVlanId) via RawFrame::send(). Returns false only if
    // the send itself failed at the driver level (netif not up) - a
    // true return means "sent", never "reached the other VLAN" - see
    // the class comment above.
    bool sendDoubleTagProbe(uint16_t nativeVlanId, uint16_t targetVlanId);

    size_t sightingCount() const;
    bool getSighting(size_t index, TagSighting& out) const;  // most-recently-seen first

private:
    static constexpr size_t kMaxSightings = 20;

    static void taskEntry(void* arg);
    void run();
    static void promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type);
    void onPromiscuousFrame(const uint8_t* payload, uint16_t len);
    void observe(const uint8_t mac[6], uint16_t outerVlanId, bool doubleTagged, uint16_t innerVlanId);
    void notify(const String& text);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<TagSighting> _sightings;
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
};

extern VlanHopProbe g_vlanHopProbe;
