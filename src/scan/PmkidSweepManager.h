#pragma once

#include <Arduino.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// "PMKID SWEEP": runs PmkidManager against every eligible AP currently
// known to WAR DRIVING, one after another, instead of the user having to
// pick a single sighting and press P each time. Like AssessmentRunner/
// DiscoveryRunner, this only DRIVES PmkidManager's existing public
// start()/isRunning() API and reimplements none of its capture logic —
// this class never touches the radio itself, never opens a pcap file
// itself, and (since Fase 36) inherits PmkidManager's own EAPOL
// structural detection for free: capture-only, never crack, exactly the
// same line PmkidManager/DeauthManager already hold.
//
// Eligible = has a real SSID (not "<hidden>" - WiFi.begin() needs a
// name to associate to) and isn't open (PMKID/handshake capture is
// meaningless against a network with no PSK to begin with). The
// snapshot of eligible sightings is taken once, at start() - APs seen
// for the first time WHILE the sweep is already running aren't added
// mid-sweep; run it again afterward to pick up anything new.
//
// Sequential, not parallel, for the same reason DiscoveryRunner
// sequences its own promiscuous phases: PmkidManager (like every other
// promiscuous consumer in this firmware) needs exclusive use of the one
// esp_wifi callback slot for the whole ~8s of one AP's capture window -
// see ui/ActivityStatus.h.
//
// stop() only stops ADVANCING to the next target once the current one's
// capture window finishes on its own - PmkidManager exposes no way to
// abort an in-flight capture early, and this class doesn't try to work
// around that.
class PmkidSweepManager {
public:
    struct SweepResult {
        String ssid;
        String bssid;
        bool pmkidCaptured = false;
        uint32_t framesCaptured = 0;
        String pcapPath;
    };

    void begin(QueueHandle_t outQueue);

    // False if already running, WiFi isn't connected (WAR DRIVING's
    // sightings would be stale/irrelevant otherwise), or no eligible
    // sighting exists yet.
    bool start();
    void stop();
    bool isRunning() const { return _running; }

    size_t targetCount() const { return _targetCount; }
    size_t currentIndex() const { return _currentIndex; }  // 0-based; == targetCount() once finished
    size_t hitCount() const { return _hitCount; }           // how many targets so far had a likely PMKID

    size_t resultCount() const;
    bool getResult(size_t index, SweepResult& out) const;  // most-recently-added first

private:
    static constexpr size_t kMaxTargets = 50;  // safety cap - see WardrivingManager::kMaxSightings for the (much larger) source pool

    struct Target {
        String ssid;
        String bssid;
        uint8_t channel = 1;
    };

    static void taskEntry(void* arg);
    void run();
    void addResult(const SweepResult& r);
    void notify(const String& text);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Target> _targets;         // built once per sweep at start() - sweep task only, no mutex needed
    std::vector<SweepResult> _results;    // read by the UI - mutex-protected
    QueueHandle_t _outQueue = nullptr;

    std::atomic<bool> _running{false};
    std::atomic<size_t> _targetCount{0};
    std::atomic<size_t> _currentIndex{0};
    std::atomic<size_t> _hitCount{0};
};

extern PmkidSweepManager g_pmkidSweepManager;
