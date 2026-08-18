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
    // NOTE: subnetBase/subnetPrefix are still unused by the discovery
    // scan itself (ScanManager::startDiscoveryScan targets the live,
    // DHCP-detected subnet — see WifiManager::networkAddress() — rather
    // than reading these), but every other field below now has a real
    // editor: SettingsScreen.
    IPAddress subnetBase{192, 168, 1, 0};  // network address, e.g. 192.168.1.0
    uint8_t subnetPrefix = 24;             // CIDR prefix length (/24 = 254 hosts)
    uint16_t portRangeStart = 1;           // editable from SettingsScreen
    uint16_t portRangeEnd = 1024;
    uint16_t scanTimeoutMs = 400;          // per-host ping / per-port connect timeout
    uint8_t maxConcurrentProbes = 4;       // rate limiting: parallel sockets in flight
    uint16_t interProbeDelayMs = 15;       // extra spacing between probes on top of concurrency
    bool autoExportOnScanFinish = false;   // auto-run ResultStore export when a discovery scan completes

    // credential audit — opt-in, defaults OFF, gated by disclaimer screen
    bool credAuditAcknowledged = false;    // user has seen & accepted the disclaimer once
    bool credAuditEnabled = false;         // must be explicitly toggled on per session

    // "offensive" tools (ARP spoof, deauth, evil twin) — same session-only
    // pattern as credAuditEnabled, but a single shared gate for all three:
    // they're all active, third-party-affecting techniques (unlike
    // credential audit, which only ever touches services already
    // discovered on a host) so the bar to unlock any of them is the same
    // strengthened disclaimer (typed confirmation, not just "Y") — see
    // OffensiveDisclaimerScreen. Never persisted, same reasoning as
    // credAuditEnabled: every boot starts back at "not enabled".
    bool offensiveAcknowledged = false;
    bool offensiveEnabled = false;

    // ui
    uint8_t uiSoundEnabled = 1;
    // Low-power mode: dims the backlight faster (see UiManager) for long
    // unattended sessions (wardriving/monitoring). Persisted.
    uint8_t lowPowerMode = 0;

    void load();
    void save() const;
    void resetToDefaults();
};

extern AppConfig g_config;
