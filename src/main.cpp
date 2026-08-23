#include <M5Unified.h>
#include <M5Cardputer.h>
#include <LittleFS.h>

#include "core/Config.h"
#include "ui/UiManager.h"
#include "ui/screens/BootScreen.h"
#include "ui/screens/MainMenuScreen.h"
#include "ui/screens/HostListScreen.h"
#include "ui/screens/WifiSetupScreen.h"
#include "ui/screens/SettingsScreen.h"
#include "scan/OuiDatabase.h"
#include "scan/PortServiceDb.h"
#include "scan/ScanManager.h"
#include "scan/PortScanManager.h"
#include "scan/CredAuditManager.h"
#include "scan/WardrivingManager.h"
#include "scan/ArpSpoofManager.h"
#include "scan/DeauthManager.h"
#include "scan/EvilTwinManager.h"
#include "scan/HttpPathBruteforcer.h"
#include "scan/PmkidManager.h"
#include "scan/CdpLldpSniffer.h"
#include "scan/BeaconProbeSniffer.h"
#include "scan/SsdpDiscovery.h"
#include "scan/RogueDhcpDetector.h"
#include "scan/SmbNegotiateCheck.h"
#include "scan/PassiveHostDiscovery.h"
#include "scan/ServiceEnumerator.h"
#include "scan/SnmpSweep.h"
#include "scan/AssessmentRunner.h"
#include "scan/DataStoreProbe.h"
#include "scan/IotOtProbe.h"
#include "scan/ServiceAuditManager.h"
#include "scan/LdapProbe.h"
#include "scan/NtlmHttpProbe.h"
#include "scan/DiscoveryRunner.h"
#include "scan/DeauthWatcher.h"
#include "scan/SentinelManager.h"
#include "scan/PmkidSweepManager.h"
#include "scan/NameSpoofManager.h"
#include "ui/screens/NameSpoofScreen.h"
#include "scan/OsFingerprint.h"
#include "scan/VlanHopProbe.h"
#include "scan/IotCredScanner.h"
#include "scan/PasswordSprayManager.h"
#include "scan/BluetoothManager.h"
#include "scan/BleGattClient.h"
#include "ui/screens/BleScannerScreen.h"
#include "net/WifiManager.h"
#include "net/CaptivePortalDetector.h"
#include "net/TimeSync.h"
#include "storage/SdCard.h"
#include "ui/screens/ScanHistoryScreen.h"
#include "ui/screens/WardrivingScreen.h"
#include "ui/screens/ThreatsScreen.h"
#include "ui/screens/AssessmentScreen.h"
#include "ui/screens/ChannelScanScreen.h"
#include "ui/screens/SentinelScreen.h"
#include "ui/screens/ActivityScreen.h"
#include "ui/screens/PlaybookScreen.h"
#include "scan/PlaybookRunner.h"
// The discovery screens (LAN TOPOLOGY / UPNP DISCOVERY / SERVICE SCAN /
// PASSIVE HOSTS / ROGUE DHCP / SNMP SWEEP / DATASTORE SWEEP) are no longer
// top-level menu entries — they're grouped under NETWORK SCAN via
// DiscoveryMenuScreen, so main.cpp doesn't reference their screens here.
// Their background managers are still begin()'d below.

namespace {
BootScreen g_bootScreen;

// Fase 54: MenuItem array is now the WIFI TOOLS submenu backing the WIFI
// tile on HomeScreen. BLE SCAN was removed from here - it now lives under
// the BLUETOOTH TOOLS tile via BluetoothToolsMenuScreen, and SETTINGS
// moved to a HomeScreen footer hotkey. So we drop from 13 entries to 11.
MenuItem g_menuItems[11];
}  // namespace

void setup() {
    auto cfg = M5.config();
    // Cardputer/Cardputer ADV have no onboard RTC chip (unlike Core2/
    // CoreS3) - external_rtc tells M5Unified's board bring-up to also
    // probe the Grove port's I2C bus for one, so a battery-backed RTC
    // Unit plugged in there gets auto-detected (M5.Rtc.isEnabled()) the
    // same way SD/IMU already degrade to "absent" instead of erroring
    // when nothing's attached. See net/TimeSync.h.
    cfg.external_rtc = true;
    M5Cardputer.begin(cfg, /*enableKeyboard=*/true);
    M5Cardputer.Display.setRotation(1);  // landscape, 240x135

    g_config.load();

    // true = format on mount failure. Only bites on a corrupt/never-
    // initialized filesystem, which on this codebase only happens on a
    // brand new board (or a partition-table change) — the OUI database
    // upload (`pio run -t uploadfs`) is what actually populates it.
    if (!LittleFS.begin(true)) {
        log_e("main: LittleFS mount failed");
    }
    LittleFS.mkdir("/history");
    sdcard::begin();  // no-op-ish if no card is inserted - see SdCard.cpp
    g_ouiDb.begin();
    g_portServiceDb.begin();

    // PORT SCANNER and CREDENTIAL AUDIT are NOT top-level menu entries:
    // both are per-host actions reached from HOST DETAIL (TAB for a port
    // scan, C for a credential audit). They used to be informational
    // PlaceholderScreen items that just explained that — removed here since
    // they started no activity of their own.
    g_menuItems[0] = {"WIFI SCAN", &WifiSetupScreen::instance()};
    g_menuItems[1] = {"NETWORK SCAN", &HostListScreen::instance()};
    g_menuItems[2] = {"AUTO ASSESS", &AssessmentScreen::instance()};
    g_menuItems[3] = {"THREATS", &ThreatsScreen::instance()};
    g_menuItems[4] = {"SCAN HISTORY", &ScanHistoryScreen::instance()};
    g_menuItems[5] = {"WAR DRIVING", &WardrivingScreen::instance()};
    g_menuItems[6] = {"CHANNEL SCAN", &ChannelScanScreen::instance()};
    g_menuItems[7] = {"SENTINEL MODE", &SentinelScreen::instance()};
    g_menuItems[8] = {"ACTIVITY", &ActivityScreen::instance()};
    g_menuItems[9] = {"PLAYBOOK", &PlaybookScreen::instance()};
    // Gated like the per-host offensive tools (MITM/deauth/PMKID/evil-
    // twin) even though it's a top-level entry: it IS the offensive
    // action itself, there's no earlier target-picking screen to gate
    // instead - see MainMenuScreen.h's MenuItem::offensive.
    g_menuItems[10] = {"NAME SPOOF", &NameSpoofScreen::instance(), true};
    // Fase 54: BLE SCAN moved under HOME -> BLUETOOTH TOOLS tile (see
    // BluetoothToolsMenuScreen); SETTINGS moved to HOME footer hotkey.
    // The discovery tools live under NETWORK SCAN -> 'D' (see
    // DiscoveryMenuScreen); their managers are still begin()'d below.
    MainMenuScreen::instance().configure(g_menuItems, 11);

    // MainMenuScreen must already be configured by this point: once the
    // render task starts, BootScreen can transition straight to it on
    // the first Enter keypress. See UiManager::begin() for why the
    // initial screen is passed in here rather than pushed afterwards.
    g_ui.begin(&g_bootScreen);

    g_scanManager.begin(g_ui.scanQueue());
    g_portScanManager.begin(g_ui.scanQueue());
    g_credAuditManager.begin(g_ui.scanQueue());
    g_wardrivingManager.begin(g_ui.scanQueue());
    g_arpSpoofManager.begin(g_ui.scanQueue());
    g_deauthManager.begin(g_ui.scanQueue());
    g_evilTwinManager.begin(g_ui.scanQueue());
    g_httpBruteforcer.begin(g_ui.scanQueue());
    g_pmkidManager.begin(g_ui.scanQueue());
    g_cdpLldpSniffer.begin(g_ui.scanQueue());
    g_beaconProbeSniffer.begin(g_ui.scanQueue());
    g_ssdpDiscovery.begin(g_ui.scanQueue());
    g_rogueDhcpDetector.begin(g_ui.scanQueue());
    g_smbCheck.begin(g_ui.scanQueue());
    g_passiveHostDiscovery.begin(g_ui.scanQueue());
    g_serviceEnumerator.begin(g_ui.scanQueue());
    g_snmpSweep.begin(g_ui.scanQueue());
    g_assessmentRunner.begin(g_ui.scanQueue());
    g_captivePortalDetector.begin(g_ui.scanQueue());
    g_dataStoreProbe.begin(g_ui.scanQueue());
    g_iotOtProbe.begin(g_ui.scanQueue());
    g_serviceAuditManager.begin(g_ui.scanQueue());
    g_ldapProbe.begin(g_ui.scanQueue());
    g_ntlmHttpProbe.begin(g_ui.scanQueue());
    g_discoveryRunner.begin(g_ui.scanQueue());
    g_deauthWatcher.begin(g_ui.scanQueue());
    g_sentinelManager.begin(g_ui.scanQueue());
    g_pmkidSweepManager.begin(g_ui.scanQueue());
    g_playbookRunner.begin(g_ui.scanQueue());
    g_nameSpoofManager.begin(g_ui.scanQueue());
    g_osFingerprint.begin(g_ui.scanQueue());
    g_vlanHopProbe.begin(g_ui.scanQueue());
    // Fase 51 - offensive credential tools, both gated by the same
    // CredAuditManager consent (AppConfig::credAuditEnabled). They reuse
    // CredAuditManager::tryLogin() for the protocol handshakes, so
    // g_credAuditManager.begin() above must run first (it does).
    g_iotCredScanner.begin(g_ui.scanQueue());
    g_passwordSpray.begin(g_ui.scanQueue());
    // Fase 52 - BLE scanner (observer-only). The NimBLE stack itself
    // is only initialized when the user starts a BLE scan (see
    // BluetoothManager::run) - this call just creates the idle task
    // and mutex, no BT init cost until requested.
    g_bluetoothManager.begin(g_ui.scanQueue());
    // Fase 53 - GATT walker (uses NimBLE central role, re-enabled via
    // platformio.ini build_flags). Same "no BT init until asked" policy:
    // begin() only wires the mutex and outQueue.
    g_bleGattClient.begin(g_ui.scanQueue());

    // Non-blocking: if a network was saved from a previous WIFI SCAN
    // run, this kicks the connection off immediately at boot instead of
    // waiting for the user to open NETWORK SCAN first. No-op if nothing
    // is saved yet (first boot, or after FORGET).
    g_wifi.autoConnect();

    // Arms the SNTP client. No-op if there's no WiFi yet - it just
    // syncs silently in the background whenever a connection exists
    // (see WifiSetupScreen for the other place this gets re-armed,
    // covering the case where autoConnect() above found nothing saved).
    TimeSync::begin();
}

void loop() {
    // All real work happens in the UI, input and scan FreeRTOS tasks
    // created in setup()/UiManager::begin(). The Arduino loopTask is
    // left idle on purpose so it never competes with them for CPU time.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
