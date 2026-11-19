#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <cstdint>

// Persistent app configuration, backed by NVS (Preferences). Small,
// flat, and loaded once at boot into RAM; screens read/write the RAM
// copy and call save() when the user confirms a change, so we are not
// hammering flash on every keypress.
struct AppConfig {
    // A9: minimum spacing between probes. 0 would make the scan loops'
    // vTaskDelay(0) a no-op (FreeRTOS does not yield on a 0 delay),
    // starving lower-priority tasks and risking a watchdog trip, so the
    // editor and load() both clamp up to this.
    static constexpr uint16_t kMinInterProbeDelayMs = 2;

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

    // WAR DRIVING "auto-scan open networks": when on, the wardrive doesn't
    // just LOG open APs, it joins each one and runs a discovery + well-
    // known-port scan against it (the same excursion that an allowlisted
    // open AP already triggers, extended to EVERY open AP). This connects
    // to and scans third-party networks the user has NOT named in the
    // allowlist, so — like offensiveEnabled — it defaults OFF, is gated by
    // its own typed-"AUTHORIZED" disclaimer (OpenAutoScanDisclaimerScreen)
    // and is NEVER persisted: every boot starts back at off. The allowlist
    // path is unaffected and keeps working when this is off.
    bool wardriveAutoScanOpenAcknowledged = false;
    bool wardriveAutoScanOpen = false;

    // ui
    uint8_t uiSoundEnabled = 1;
    // Low-power mode: dims the backlight faster (see UiManager) for long
    // unattended sessions (wardriving/monitoring). Persisted.
    uint8_t lowPowerMode = 0;

    void load();
    void save() const;
    void resetToDefaults();

    // Forces every field back into the range the rest of the firmware
    // assumes (the same bounds the SETTINGS editor enforces). load() and
    // ConfigBackup::restore() both call it, so a stale NVS value or an
    // edited/foreign backup file can't smuggle in a 0 ms timeout, zero
    // worker threads, an inverted port range or a 0 inter-probe delay.
    void clamp();
};

extern AppConfig g_config;
