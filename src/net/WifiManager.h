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
        String bssid;  // "aa:bb:cc:dd:ee:ff" - identifies the physical AP, not just the network name
        int32_t rssi = 0;
        uint8_t channel = 0;
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

    // Saves (ssid, password) as the most-recently-used network. If ssid
    // already has a saved entry, it's updated in place and moved to the
    // front; otherwise it's inserted at the front, evicting the
    // least-recently-used entry once kMaxSavedNetworks is reached. See
    // WifiManager.cpp for the on-disk layout.
    void saveCredentials(const String& ssid, const String& password);

    bool hasSavedCredentials() const;
    String savedSsid() const;  // most-recently-used, display only — never exposes the password
    void forgetSavedCredentials();  // forgets ALL saved networks

    // Up to kMaxSavedNetworks networks, most-recently-used first (index
    // 0 == savedSsid()) — backs WifiSetupScreen's quick-switch list, so
    // reconnecting to a network typed in before never needs its
    // password retyped.
    static constexpr uint8_t kMaxSavedNetworks = 3;
    uint8_t savedNetworkCount() const;
    String savedNetworkSsid(uint8_t index) const;  // "" if index is out of range

    // Only for storage/ConfigBackup (backup/restore to SD) — every
    // other consumer in this codebase deliberately never sees a saved
    // password (see savedSsid()'s comment above). "" if index is out
    // of range.
    String savedNetworkPassword(uint8_t index) const;

    // Non-blocking, like beginConnectWithCredentials() — kicks off a
    // connection attempt using an already-saved network's stored
    // password. Returns false (no-op) if index is out of range.
    bool connectSaved(uint8_t index);

    // Re-inserts an already-saved network at the front of the
    // most-recently-used list using its own stored password (never
    // touches the password) — call this instead of saveCredentials()
    // when reconnecting via connectSaved() succeeded, so the list
    // reflects actual recency of use without risking overwriting a
    // real password with an empty one.
    void touchSavedNetwork(uint8_t index);

    void forgetSavedNetwork(uint8_t index);  // forgets just this one

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
