#pragma once

#include <Arduino.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Active evil-twin AP: stands up a WiFi access point with a
// user-chosen SSID (typically one seen in WAR DRIVING, to test how
// readily nearby clients auto-reconnect to a look-alike network) and
// logs every device that associates — MAC address + timestamp only.
// Deliberately does NOT run a captive portal, does NOT ask for or
// process any credentials, and does NOT proxy a connected client's
// traffic anywhere — the entire point is "did a client connect to this
// fake AP at all", not harvesting anything once it has. See
// WardrivingManager's own evil-twin *detection* heuristic for the
// passive counterpart to this active one.
//
// KARMA MODE (startKarma()): a second way to start the same AP
// lifecycle, using a SSID this device picks and cycles through itself
// instead of one the user types. A REAL Karma attack answers every
// client's probe request individually and instantly with a forged
// probe response claiming whatever SSID it just asked for — that needs
// low-level control over per-client probe responses the stock Arduino/
// esp-idf softAP API doesn't expose. What this does instead, and is
// honest about being a coarser approximation: snapshot the SSIDs
// BeaconProbeSniffer has already seen nearby clients probe for (their
// PNL - see BeaconProbeSniffer.h), then cycle this device's ONE softAP
// through that candidate list a few seconds at a time, hoping an
// in-range device with a matching entry in ITS PNL auto-associates
// during that window. Slower and less certain than a true Karma
// attack, but buildable with the same AP-lifecycle/association-logging
// code the fixed-SSID mode above already has — see startKarma()'s own
// comment for the exact snapshot/cycling mechanics.
//
// Always broadcasts OPEN (no password) regardless of the real network's
// actual encryption — matching a WPA2 passphrase this device was never
// given isn't possible, and the interesting finding is the same either
// way: whether a client reconnects to a same-named network with weaker
// (or no) security than the real one, without the user noticing the
// downgrade.
//
// Hardware limitation, not a bug: ESP32 concurrent AP+STA mode shares
// one radio, so both interfaces are forced onto the SAME channel. If
// this device is already connected (as STA) to its own WiFi network,
// the evil-twin AP actually ends up on THAT network's channel, not
// necessarily the `channel` requested here — best-effort only, surfaced
// on-screen rather than hidden.
class EvilTwinManager {
public:
    struct Association {
        String mac;
        String ssid;  // which candidate SSID this client associated to - fixed-mode sessions repeat the same value
        uint32_t atMs = 0;
    };

    void begin(QueueHandle_t outQueue);

    bool start(const String& ssid, uint8_t channel);
    void stop();
    bool isRunning() const { return _running; }
    String ssid() const { return _ssid; }  // the SSID currently broadcasting - changes over time in karma mode

    // Read-only preview of what startKarma() would cycle through, safe
    // to call any time - the SSIDs nearby clients have been overheard
    // probing for (BeaconProbeSniffer's PNL data), deduplicated, capped
    // at kMaxKarmaCandidates. Lets the UI show what's about to happen
    // (or that there's nothing to impersonate yet) before committing.
    size_t previewKarmaCandidates(std::vector<String>& out) const;

    // False if already running or no candidate SSID is available yet
    // (run BEACON/PROBE INTEL first - see class comment). Snapshots the
    // candidate list once, at call time, same as previewKarmaCandidates().
    bool startKarma(uint8_t channel);
    bool isKarmaMode() const { return _karmaMode; }
    size_t karmaCandidateCount() const { return _karmaCandidates.size(); }
    size_t karmaCurrentIndex() const { return _karmaIndex; }

    size_t associationCount() const;
    bool getAssociation(size_t index, Association& out) const;  // most-recent-first

private:
    static constexpr size_t kMaxKarmaCandidates = 15;
    static constexpr uint32_t kKarmaDwellMs = 8000;  // per-candidate broadcast window before cycling to the next

    static void taskEntry(void* arg);
    void run();
    void advanceKarmaCandidate();
    void logAssociation(const String& mac);
    void notify(const String& text);
    void notify(ScanEventType type, uint8_t pct = 0);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Association> _associations;
    std::vector<String> _seenMacs;  // dedup - log each station once per session
    std::vector<String> _karmaCandidates;
    size_t _karmaIndex = 0;
    bool _karmaMode = false;
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
    String _ssid;
    uint8_t _channel = 1;
};

extern EvilTwinManager g_evilTwinManager;
