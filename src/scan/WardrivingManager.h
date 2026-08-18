#pragma once

#include <IPAddress.h>
#include <WiFi.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Continuous background WiFi survey ("war driving"): repeatedly scans
// for nearby access points and logs every one seen this session (SSID,
// BSSID, RSSI, channel, encryption, OUI vendor off the BSSID) to the SD
// card (or LittleFS - see storage/SdCard.h) — purely passive, this part
// never connects to anything.
//
// A SEPARATE, explicit allowlist of SSIDs (added by the user from
// WardrivingScreen, NVS-persisted, own namespace) controls the one
// active behavior this module has: an open (unencrypted) network whose
// SSID is on that allowlist gets automatically joined, discovery- and
// port-scanned (reusing ScanManager/PortScanManager — the exact same
// modules NETWORK SCAN/PORT SCANNER already drive), the results saved
// under /netrunner/ (see storage/NetrunnerPaths.h) alongside every other
// scan report this firmware produces, then the device disconnects and
// reconnects to whatever network it had saved before.
//
// This is deliberately NOT "every open network encountered" the way it
// might first sound like war-driving should work: scanning a stranger's
// network without their consent is not something this assistant will
// automate indiscriminately, no matter how the encryption on it happens
// to be set. The allowlist is what turns this from "attack whatever
// open AP happens to be nearby" into "automate testing across networks
// I've told this tool I'm explicitly authorized for" — the same shape
// of scoping every other active module in this firmware already
// requires (see CredDisclaimerScreen). See README for the full
// reasoning.
class WardrivingManager {
public:
    struct ApSighting {
        String ssid;
        String bssid;
        int32_t rssi = 0;
        uint8_t channel = 0;
        wifi_auth_mode_t encryption = WIFI_AUTH_OPEN;
        String vendor;             // OUI lookup off the BSSID, best-effort
        uint32_t firstSeenMs = 0;
        uint32_t lastSeenMs = 0;
        bool open = false;
        bool allowlisted = false;
        bool discovered = false;   // true once an allow-listed open AP has had discovery run against it this session

        // Possible evil twin: another sighting exists with the SAME
        // SSID but a DIFFERENT BSSID, and either a meaningfully weaker/
        // stronger encryption tier or a different (known-on-both-sides)
        // OUI vendor - the classic "clone a real network's name" attack,
        // caught whether the clone matches the encryption or not. See
        // WardrivingManager::runScanCycle for the exact rule (and why
        // channel is deliberately not part of it). Deliberately not
        // asserting which of the two (if either) is the impostor -
        // order alone can't tell that apart.
        bool suspicious = false;
        String suspiciousNote;
    };

    void begin(QueueHandle_t outQueue);

    void start();  // no-op if already running
    void stop();
    bool isRunning() const { return _running; }

    // Most-recently-seen first.
    size_t sightingCount() const;
    bool getSighting(size_t index, ApSighting& out) const;

    uint32_t openCount() const { return _openCount; }
    uint32_t discoveredCount() const { return _discoveredCount; }
    uint32_t suspiciousCount() const { return _suspiciousCount; }

    // Allowlist management (NVS-backed, own namespace) — SSIDs here are
    // networks the user has told this tool they own or are explicitly
    // authorized to test; see the class comment above. Callers (see
    // WardrivingScreen) are expected to show an explicit warning before
    // calling addToAllowlist() — this class doesn't gate it itself.
    static constexpr uint8_t kMaxAllowlist = 10;
    uint8_t allowlistCount() const;
    String allowlistSsid(uint8_t index) const;
    bool addToAllowlist(const String& ssid);  // false if full or already present
    void removeFromAllowlist(uint8_t index);
    bool isAllowlisted(const String& ssid) const;

private:
    static constexpr size_t kMaxSightings = 300;
    static constexpr uint32_t kScanIntervalMs = 15000;

    static void taskEntry(void* arg);
    void run();
    void runScanCycle();
    void handleOpenAllowlistedAp(const ApSighting& ap);
    void logSighting(const ApSighting& ap);
    void notify(const String& text);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<ApSighting> _sightings;
    QueueHandle_t _outQueue = nullptr;

    std::atomic<bool> _running{false};
    std::atomic<uint32_t> _openCount{0};
    std::atomic<uint32_t> _discoveredCount{0};
    std::atomic<uint32_t> _suspiciousCount{0};
};

extern WardrivingManager g_wardrivingManager;
