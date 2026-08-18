#pragma once

#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <esp_wifi.h>

// Passive rogue-DHCP-server detector: listens (via the same
// promiscuous capture machinery as ArpSpoofManager/DeauthManager/
// PmkidManager/CdpLldpSniffer — see ArpSpoofManager.h's RISK block and
// its shared-radio caveat, which applies here too) for DHCPOFFER
// replies and flags any that come from an IP other than the currently-
// known gateway. A second/unexpected DHCP server answering client
// requests is a classic sign of either a misconfigured device or an
// active MITM setup (a rogue DHCP server can hand out itself as the
// gateway/DNS server to redirect a victim's traffic) — this class only
// ever reports the finding, it never acts on it (no active response,
// no DHCP client behavior of its own beyond what the ESP32's normal
// WiFi connection already does).
class RogueDhcpDetector {
public:
    struct Sighting {
        IPAddress serverIp;
        IPAddress offeredIp;
        uint32_t lastSeenMs = 0;
        bool suspicious = false;  // serverIp != the gateway this device is actually using
    };

    void begin(QueueHandle_t outQueue);

    void start();  // no-op if already running
    void stop();
    bool isRunning() const { return _running; }

    size_t sightingCount() const;
    bool getSighting(size_t index, Sighting& out) const;  // most-recently-seen first

private:
    static constexpr size_t kMaxSightings = 20;

    static void taskEntry(void* arg);
    void run();
    static void promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type);
    void onPromiscuousFrame(const uint8_t* payload, uint16_t len);
    void addSighting(const IPAddress& serverIp, const IPAddress& offeredIp);
    void notify(const String& text);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Sighting> _sightings;
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
};

extern RogueDhcpDetector g_rogueDhcpDetector;
