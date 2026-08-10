#include <M5Unified.h>
#include <M5Cardputer.h>
#include <LittleFS.h>

#include "core/Config.h"
#include "ui/UiManager.h"
#include "ui/screens/BootScreen.h"
#include "ui/screens/MainMenuScreen.h"
#include "ui/screens/PlaceholderScreen.h"
#include "ui/screens/HostListScreen.h"
#include "scan/OuiDatabase.h"
#include "scan/ScanManager.h"
#include "scan/PortScanManager.h"
#include "scan/CredAuditManager.h"

namespace {
BootScreen g_bootScreen;

// Placeholder targets for modules that land in later development
// phases (see README.md roadmap). Swapping a MenuItem's target to a
// real screen is the only change needed once a module ships.
PlaceholderScreen g_portScanScreen;
PlaceholderScreen g_credAuditScreen;
PlaceholderScreen g_settingsScreen;

MenuItem g_menuItems[4];
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

    g_ui.begin();
    g_scanManager.begin(g_ui.scanQueue());
    g_portScanManager.begin(g_ui.scanQueue());
    g_credAuditManager.begin(g_ui.scanQueue());

    g_portScanScreen.configure(
        "PORT SCANNER",
        "Port scanning targets one host at a time: run NETWORK SCAN, select a "
        "discovered host, then press TAB on its detail screen.");
    g_credAuditScreen.configure(
        "CREDENTIAL AUDIT",
        "Also per-host: from a host's detail screen, press C. You'll see an "
        "authorization disclaimer the first time each session before anything "
        "runs. Checks a small fixed dictionary of well-known default "
        "credentials only - never a generic brute-force.");
    g_settingsScreen.configure(
        "SETTINGS",
        "Subnet / port range / rate-limit configuration arrives together with the scan modules that use it.");

    g_menuItems[0] = {"NETWORK SCAN", &HostListScreen::instance()};
    g_menuItems[1] = {"PORT SCANNER", &g_portScanScreen};
    g_menuItems[2] = {"CREDENTIAL AUDIT", &g_credAuditScreen};
    g_menuItems[3] = {"SETTINGS", &g_settingsScreen};
    MainMenuScreen::instance().configure(g_menuItems, 4);

    g_ui.pushScreen(&g_bootScreen);
}

void loop() {
    // All real work happens in the UI, input and scan FreeRTOS tasks
    // created in setup()/UiManager::begin(). The Arduino loopTask is
    // left idle on purpose so it never competes with them for CPU time.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
