#pragma once

#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <esp_wifi.h>

// Passive host discovery: learns which hosts exist on the local segment
// purely by *listening* to their traffic in promiscuous mode — no ARP
// sweep, no ping, no probe of any kind leaves this device. Every plain
// 802.11 data frame carrying an IPv4 packet yields a (source MAC,
// source IP) pair; those pairs are collected here, so a host that never
// answers an active probe (firewalled, or asleep during the sweep) still
// shows up the moment it sends anything.
//
// Deliberately kept as its own list with its own screen rather than
// merged into ScanManager's host table: that table is built by the
// active sweep with generation/mutex logic tuned to it, and quietly
// injecting rows from a background listener would tangle with that. The
// value here — "who is talking on this network right now, without me
// making a sound" — stands on its own.
//
// Same shared-radio caveat as every other promiscuous feature (ARP/MITM,
// deauth, PMKID, CDP/LLDP, rogue DHCP): esp_wifi allows only one
// promiscuous callback at a time, and only OPEN networks are readable
// (WPA frames are encrypted). See ArpSpoofManager.h's RISK block.
class PassiveHostDiscovery {
public:
    struct Observed {
        IPAddress ip;
        uint8_t mac[6] = {0};
        bool macKnown = false;
        uint32_t frames = 0;
        uint32_t lastSeenMs = 0;
    };

    void begin(QueueHandle_t outQueue);

    void start();  // no-op if already running
    void stop();
    bool isRunning() const { return _running; }

    size_t count() const;
    bool get(size_t index, Observed& out) const;  // most-recently-seen first

private:
    static constexpr size_t kMaxHosts = 60;

    static void taskEntry(void* arg);
    void run();
    static void promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type);
    void onPromiscuousFrame(const uint8_t* payload, uint16_t len);
    void observe(const IPAddress& ip, const uint8_t mac[6]);
    void notify(const String& text);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Observed> _hosts;
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
};

extern PassiveHostDiscovery g_passiveHostDiscovery;
