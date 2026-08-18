#pragma once

#include <Arduino.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <esp_wifi.h>

// Passive CDP (Cisco Discovery Protocol) / LLDP (Link Layer Discovery
// Protocol) sniffer — reveals nearby switch/router identity and the
// port a device is plugged into, straight off the wire, without
// sending a single packet. Purely passive, same philosophy as
// WardrivingManager: begin()/start()/stop() toggle, no active behavior
// at all here (unlike WardrivingManager, this class has no allowlisted
// "active" mode either — there's nothing to actively do with CDP/LLDP
// information beyond reading it).
//
// REAL LIMITATION, not a bug: CDP/LLDP are switch-to-switch discovery
// protocols, normally exchanged only between directly-connected wired
// ports. A WiFi station only sees them at all if the AP happens to
// bridge those specific multicast MACs onto the wireless segment
// (CDP: 01:00:0C:CC:CC:CC, LLDP: 01:80:C2:00:00:0E) — plenty of
// consumer/prosumer APs do this since it's just regular multicast
// forwarding to them, but plenty of others filter it. Seeing nothing
// isn't necessarily proof there's no CDP/LLDP-speaking gear nearby.
//
// Uses the same promiscuous-capture machinery as ArpSpoofManager/
// DeauthManager/PmkidManager (see ArpSpoofManager.h's RISK block) —
// and shares its single real constraint with them too:
// esp_wifi_set_promiscuous_rx_cb() only ever has ONE callback
// registered at a time, so running this at the same time as any of
// those three silently steals/loses frames from whichever one starts
// second. Same "one shared radio" limitation already accepted
// elsewhere in this firmware (WardrivingManager vs. NETWORK SCAN,
// etc.) — not arbitrated in software, just don't run two promiscuous
// features at once.
class CdpLldpSniffer {
public:
    struct Neighbor {
        String deviceId;  // CDP Device ID / LLDP Chassis ID, best-effort text
        String portId;    // CDP Port ID / LLDP Port ID, best-effort text
        bool isCdp = false;  // false = LLDP
        uint32_t lastSeenMs = 0;
    };

    void begin(QueueHandle_t outQueue);

    void start();  // no-op if already running
    void stop();
    bool isRunning() const { return _running; }

    size_t neighborCount() const;
    bool getNeighbor(size_t index, Neighbor& out) const;  // most-recently-seen first

private:
    static constexpr size_t kMaxNeighbors = 40;

    static void taskEntry(void* arg);
    void run();
    static void promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type);
    void onPromiscuousFrame(const uint8_t* payload, uint16_t len);
    void parseCdp(const uint8_t* p, uint16_t len);
    void parseLldp(const uint8_t* p, uint16_t len);
    void addOrUpdateNeighbor(const String& deviceId, const String& portId, bool isCdp);
    void notify(const String& text);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Neighbor> _neighbors;
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
};

extern CdpLldpSniffer g_cdpLldpSniffer;
