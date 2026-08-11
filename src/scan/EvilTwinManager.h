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
        uint32_t atMs = 0;
    };

    void begin(QueueHandle_t outQueue);

    bool start(const String& ssid, uint8_t channel);
    void stop();
    bool isRunning() const { return _running; }
    String ssid() const { return _ssid; }

    size_t associationCount() const;
    bool getAssociation(size_t index, Association& out) const;  // most-recent-first

private:
    static void taskEntry(void* arg);
    void run();
    void logAssociation(const String& mac);
    void notify(const String& text);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Association> _associations;
    std::vector<String> _seenMacs;  // dedup - log each station once per session
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
    String _ssid;
    uint8_t _channel = 1;
};

extern EvilTwinManager g_evilTwinManager;
