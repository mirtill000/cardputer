#pragma once

#include <Arduino.h>
#include <atomic>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// "Run all discovery": one action, from the DISCOVERY submenu, that
// exercises every discovery tool in turn and leaves each tool's own
// screen populated with what it found.
//
// NOT truly simultaneous, on purpose — it CAN'T be: three of the tools
// (LAN topology, passive hosts, rogue DHCP) share the single esp_wifi
// promiscuous callback (see ArpSpoofManager.h's shared-radio note), so
// running two at once just starves all but the last. This runner instead
// SEQUENCES them: the one-shot UDP/TCP queries (UPnP/SSDP, mDNS services,
// SNMP sweep, data-store sweep) run one after another, then each
// promiscuous listener gets its own timed capture window. The result is
// "one button collects everything" without violating the radio
// constraint the rest of the firmware is careful about.
//
// Like AssessmentRunner, it only DRIVES the existing managers via their
// public start()/stop()/isRunning() APIs and reimplements none of them.
class DiscoveryRunner {
public:
    enum class Phase : uint8_t {
        Idle,
        Upnp,
        Services,
        Snmp,
        DataStore,
        LanTopology,
        PassiveHosts,
        RogueDhcp,
        Done,
        Failed,
    };

    void begin(QueueHandle_t outQueue);

    bool start();  // no-op if already running
    void stop();   // request cancellation
    bool isRunning() const { return _running; }

    Phase phase() const { return _phase; }
    uint8_t progressPct() const { return _progressPct; }

private:
    // How long each promiscuous listener gets to capture before the runner
    // stops it and moves on to the next one.
    static constexpr uint32_t kPromiscWindowMs = 12000;
    // Safety cap so a one-shot query that never clears its running flag
    // can't wedge the whole run.
    static constexpr uint32_t kOneShotMaxMs = 30000;

    static void taskEntry(void* arg);
    void run();
    void waitOneShot(bool (*isRunning)());
    void sleepWindow(uint32_t ms);
    void setPhase(Phase p, const String& msg, uint8_t pct);
    void notify(const String& text);
    void notify(ScanEventType type, uint8_t pct = 0);

    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
    std::atomic<Phase> _phase{Phase::Idle};
    std::atomic<uint8_t> _progressPct{0};
};

extern DiscoveryRunner g_discoveryRunner;
