#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <cstdint>

// Persistent app configuration, backed by NVS (Preferences). Small,
// flat, and loaded once at boot into RAM; screens read/write the RAM
// copy and call save() when the user confirms a change, so we are not
// hammering flash on every keypress.
struct AppConfig {
    // net/scan
    // NOTE: subnetBase/subnetPrefix are reserved for a future Settings
    // screen that lets the user override the scan range manually. The
    // discovery scan (ScanManager::startDiscoveryScan) currently always
    // targets the live, DHCP-detected subnet of whatever network the
    // device is connected to (see WifiManager::networkAddress()) rather
    // than reading these — that covers the common "audit the network
    // I'm on" case without needing a numeric IP/prefix editor UI yet.
    IPAddress subnetBase{192, 168, 1, 0};  // network address, e.g. 192.168.1.0
    uint8_t subnetPrefix = 24;             // CIDR prefix length (/24 = 254 hosts)
    uint16_t portRangeStart = 1;           // used as-is by PortScanScreen; no range editor UI yet either
    uint16_t portRangeEnd = 1024;
    uint16_t scanTimeoutMs = 400;          // per-host ping / per-port connect timeout
    uint8_t maxConcurrentProbes = 4;       // rate limiting: parallel sockets in flight
    uint16_t interProbeDelayMs = 15;       // extra spacing between probes on top of concurrency

    // credential audit — opt-in, defaults OFF, gated by disclaimer screen
    bool credAuditAcknowledged = false;    // user has seen & accepted the disclaimer once
    bool credAuditEnabled = false;         // must be explicitly toggled on per session

    // ui
    uint8_t uiSoundEnabled = 1;
    uint8_t rainDensity = 6;               // active matrix-rain columns out of 40 max (240px / 6px glyph width)

    void load();
    void save() const;
    void resetToDefaults();
};

extern AppConfig g_config;
