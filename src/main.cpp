#include <M5Unified.h>
#include <M5Cardputer.h>

#include "core/Config.h"
#include "ui/UiManager.h"
#include "ui/screens/BootScreen.h"
#include "ui/screens/MainMenuScreen.h"
#include "ui/screens/PlaceholderScreen.h"

namespace {
BootScreen g_bootScreen;

// Placeholder targets for modules that land in later development
// phases (see README.md roadmap). Swapping a MenuItem's target to a
// real screen is the only change needed once a module ships.
PlaceholderScreen g_netScanScreen;
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

    g_netScanScreen.configure(
        "NETWORK SCAN",
        "ARP + ping sweep host discovery lands in the next development phase. Menu wiring is ready.");
    g_portScanScreen.configure(
        "PORT SCANNER",
        "TCP connect scan with banner grabbing lands in a later phase.");
    g_credAuditScreen.configure(
        "CREDENTIAL AUDIT",
        "Default-credential checks are opt-in and gated behind an authorization disclaimer. Added last, on purpose.");
    g_settingsScreen.configure(
        "SETTINGS",
        "Subnet / port range / rate-limit configuration arrives together with the scan modules that use it.");

    g_menuItems[0] = {"NETWORK SCAN", &g_netScanScreen};
    g_menuItems[1] = {"PORT SCANNER", &g_portScanScreen};
    g_menuItems[2] = {"CREDENTIAL AUDIT", &g_credAuditScreen};
    g_menuItems[3] = {"SETTINGS", &g_settingsScreen};
    MainMenuScreen::instance().configure(g_menuItems, 4);

    g_ui.begin();
    g_ui.pushScreen(&g_bootScreen);
}

void loop() {
    // All real work happens in the UI, input and (later) scan FreeRTOS
    // tasks created in UiManager::begin(). The Arduino loopTask is left
    // idle on purpose so it never competes with them for CPU time.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
