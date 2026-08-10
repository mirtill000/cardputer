#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <cstdint>

// STA connection + the subnet math the scan modules build their host
// range from. Credentials come from include/secrets.h (gitignored; copy
// include/secrets.h.example) rather than an on-device provisioning UI —
// a deliberate scope cut for this phase, see README roadmap.
class WifiManager {
public:
    // Non-blocking: kicks off the connection attempt and returns
    // immediately. Safe to call repeatedly (e.g. once per screen entry,
    // as a simple retry) — WiFi.begin() just restarts the attempt.
    // Poll isConnected() to find out when/if it succeeded.
    void beginConnect();

    // Blocks until connected or timeoutMs elapses. Only safe to call
    // from a background task (e.g. a scan worker) — never from the UI
    // task, which would freeze rendering/input for the whole timeout.
    bool connect(uint32_t timeoutMs = 15000);

    bool isConnected() const;

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

    static constexpr uint32_t kMaxScanHosts = 512;
};

extern WifiManager g_wifi;
