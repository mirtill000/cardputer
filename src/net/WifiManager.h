#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <cstdint>
#include <vector>
// Pulled in (rather than the narrower esp_wifi_types.h) just for
// wifi_auth_mode_t: which ESP-IDF header actually declares that type
// has moved around between IDF versions, while WiFi.h itself — already
// used everywhere else in this codebase without issue — is guaranteed
// to bring it in transitively, since WiFi.encryptionType() returns
// exactly this type.
#include <WiFi.h>

// STA connection + network scanning + the subnet math the scan modules
// build their host range from.
//
// Credentials are never hardcoded: the user picks a network via
// WifiSetupScreen (scan -> select -> type password on the physical
// keyboard) and, on a successful connect, they're persisted to NVS
// (Preferences, namespace "wifi") so the device reconnects on its own
// next boot. NVS on ESP32 is NOT encrypted by default — this is the
// same trust model as everything else this firmware stores in flash
// (physical possession of the device implies access to what's on it);
// see README for the full note.
class WifiManager {
public:
    struct ScanResult {
        String ssid;
        int32_t rssi = 0;
        wifi_auth_mode_t encryption = WIFI_AUTH_OPEN;
    };

    // Tries to connect with whatever credentials are saved in NVS, if
    // any. No-op (and returns false) if nothing is saved — the caller
    // is expected to send the user to WifiSetupScreen in that case.
    // Non-blocking either way.
    bool autoConnect();

    // Non-blocking: kicks off a connection attempt with explicit
    // credentials and returns immediately. Does NOT save them by
    // itself — the caller (WifiSetupScreen) calls saveCredentials()
    // once it observes isConnected() == true, so a wrong password
    // never gets written to NVS just because someone tried it.
    void beginConnectWithCredentials(const String& ssid, const String& password);
    void saveCredentials(const String& ssid, const String& password);

    bool hasSavedCredentials() const;
    String savedSsid() const;  // display only — never exposes the password
    void forgetSavedCredentials();

    bool isConnected() const;
    // True once the driver has definitively given up on the current
    // connection attempt (bad password, SSID not in range) rather than
    // still being in progress — lets WifiSetupScreen report failure
    // immediately instead of only after a fixed timeout.
    bool connectFailed() const;
    String currentSsid() const;

    IPAddress localIP() const;
    IPAddress subnetMask() const;
    IPAddress gatewayIP() const;

    // Network address of the connected subnet (e.g. 192.168.1.0 for a
    // /24), and how many usable host addresses (excluding network +
    // broadcast) it has. hostCount() is clamped to kMaxScanHosts: a
    // misconfigured /8 network has 16M+ addresses, and scanning that
    // many one by one is not something this MCU should attempt — a
    // real Settings screen (future work) would let the user narrow the
    // range explicitly instead of silently truncating it like this.
    IPAddress networkAddress() const;
    uint32_t hostCount() const;

    // Async network scan. Call beginScan() once, then poll
    // scanStatus() each frame: kScanRunning while in progress,
    // kScanFailed if it never started/errored, or >=0 once complete
    // (the number of networks found — possibly 0). Results stay valid
    // until the next beginScan() call.
    static constexpr int16_t kScanRunning = -1;
    static constexpr int16_t kScanFailed = -2;

    void beginScan();
    int16_t scanStatus() const;
    bool getScanResult(int16_t index, ScanResult& out) const;

    static constexpr uint32_t kMaxScanHosts = 512;
};

extern WifiManager g_wifi;
