#pragma once

#include <Arduino.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <esp_wifi.h>

// "GUARD MODE": passive deauthentication/disassociation-flood detector -
// the defensive counterpart to DeauthManager. Instead of SENDING deauth
// frames, this only ever COUNTS them, on whichever channel this device's
// STA is currently associated to (same "stay on the current channel"
// choice as CdpLldpSniffer/RogueDhcpDetector/PassiveHostDiscovery, not
// BeaconProbeSniffer's channel-hopping - this watches traffic on the
// network you're actually on right now, not a full-spectrum survey).
//
// A real deauth-flood tool (aireplay-ng --deauth, a Pwnagotchi, ...)
// sends many frames per second, continuously - a world apart from the
// occasional isolated deauth/disassoc frame that's completely normal
// 802.11 housekeeping (an AP dropping an idle client, a phone roaming
// away). This buckets frames per-BSSID in a rolling window and only
// alerts once a real-attack-shaped rate is crossed (see kFloodThreshold)
// - see the .cpp for the exact numbers and the reasoning.
//
// NEVER TRANSMITS ANYTHING - strictly receive-only, same as
// BeaconProbeSniffer. No offensive-tools gate needed for the same
// reason: this only ever listens, never acts on a third-party device.
//
// Shares the single esp_wifi promiscuous callback with every other
// promiscuous consumer in this firmware (ArpSpoofManager/DeauthManager/
// PmkidManager/CdpLldpSniffer/RogueDhcpDetector/PassiveHostDiscovery/
// BeaconProbeSniffer - see ArpSpoofManager.h's RISK block / ui/
// ActivityStatus.h). Running this alongside any of them silently steals/
// loses frames from whichever one started second.
class DeauthWatcher {
public:
    struct Incident {
        String bssid;            // AP address (Addr3) the deauth/disassoc frames reference
        uint32_t count = 0;       // total matching frames this session for this BSSID
        uint32_t windowCount = 0; // matching frames in the CURRENT rolling window
        bool flooding = false;    // true once windowCount has crossed kFloodThreshold and hasn't dropped back below since
        uint32_t firstSeenMs = 0;
        uint32_t lastSeenMs = 0;
    };

    void begin(QueueHandle_t outQueue);

    void start();  // no-op if already running
    void stop();
    bool isRunning() const { return _running; }

    uint32_t totalFrames() const { return _totalFrames; }
    bool anyFlooding() const { return _anyFlooding; }

    size_t incidentCount() const;
    bool getIncident(size_t index, Incident& out) const;  // most-recently-added first

private:
    static constexpr size_t kMaxIncidents = 20;
    static constexpr uint32_t kWindowMs = 10000;     // rolling window length
    static constexpr uint32_t kFloodThreshold = 15;  // frames/window against ONE bssid to call it a flood

    static void taskEntry(void* arg);
    void run();
    static void promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type);
    void onManagementFrame(const uint8_t* p, uint16_t len);
    void rollWindow();
    void notify(const String& text);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Incident> _incidents;
    QueueHandle_t _outQueue = nullptr;

    std::atomic<bool> _running{false};
    std::atomic<uint32_t> _totalFrames{0};
    std::atomic<bool> _anyFlooding{false};
    uint32_t _windowStartMs = 0;
};

extern DeauthWatcher g_deauthWatcher;
