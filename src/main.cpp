#include <M5Unified.h>
#include <M5Cardputer.h>
#include <LittleFS.h>

#include "core/Config.h"
#include "ui/UiManager.h"
#include "ui/screens/BootScreen.h"
#include "ui/screens/MainMenuScreen.h"
#include "ui/screens/PlaceholderScreen.h"
#include "ui/screens/HostListScreen.h"
#include "ui/screens/WifiSetupScreen.h"
#include "ui/screens/SettingsScreen.h"
#include "scan/OuiDatabase.h"
#include "scan/ScanManager.h"
#include "scan/PortScanManager.h"
#include "scan/CredAuditManager.h"
#include "net/WifiManager.h"

namespace {
BootScreen g_bootScreen;

// Placeholder targets for modules that land in later development
// phases (see README.md roadmap). Swapping a MenuItem's target to a
// real screen is the only change needed once a module ships.
PlaceholderScreen g_portScanScreen;
PlaceholderScreen g_credAuditScreen;

MenuItem g_menuItems[5];
}  // namespace

void setup() {
    auto cfg = M5.config();
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
    g_ouiDb.begin();

    g_portScanScreen.configure(
        "PORT SCANNER",
        "Port scanning targets one host at a time: run NETWORK SCAN, select a "
        "discovered host, then press TAB on its detail screen.");
    g_credAuditScreen.configure(
        "CREDENTIAL AUDIT",
        "Also per-host: from a host's detail screen, press C. You'll see an "
        "authorization disclaimer the first time each session before anything "
        "runs - this is a real credential-attack tool (built-in defaults plus "
        "your own wordlists), not just a known-defaults check.");

    g_menuItems[0] = {"WIFI SETUP", &WifiSetupScreen::instance()};
    g_menuItems[1] = {"NETWORK SCAN", &HostListScreen::instance()};
    g_menuItems[2] = {"PORT SCANNER", &g_portScanScreen};
    g_menuItems[3] = {"CREDENTIAL AUDIT", &g_credAuditScreen};
    g_menuItems[4] = {"SETTINGS", &SettingsScreen::instance()};
    MainMenuScreen::instance().configure(g_menuItems, 5);

    // MainMenuScreen must already be configured by this point: once the
    // render task starts, BootScreen can transition straight to it on
    // the first Enter keypress. See UiManager::begin() for why the
    // initial screen is passed in here rather than pushed afterwards.
    g_ui.begin(&g_bootScreen);

    g_scanManager.begin(g_ui.scanQueue());
    g_portScanManager.begin(g_ui.scanQueue());
    g_credAuditManager.begin(g_ui.scanQueue());

    // Non-blocking: if a network was saved from a previous WIFI SETUP
    // run, this kicks the connection off immediately at boot instead of
    // waiting for the user to open NETWORK SCAN first. No-op if nothing
    // is saved yet (first boot, or after FORGET).
    g_wifi.autoConnect();
}

void loop() {
    // All real work happens in the UI, input and scan FreeRTOS tasks
    // created in setup()/UiManager::begin(). The Arduino loopTask is
    // left idle on purpose so it never competes with them for CPU time.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
