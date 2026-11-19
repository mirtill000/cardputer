#include "OpenAutoScanDisclaimerScreen.h"
#include "../TextWrap.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Config.h"

namespace {
const char* kDisclaimer =
    "This JOINS every OPEN network found and runs a host + well-known-port "
    "scan on each - not only allowlisted ones. Connecting to and scanning "
    "a network you don't own or aren't explicitly authorized for is "
    "illegal in most places, separately from any wiretapping law. Leave "
    "this OFF and use the allowlist unless you are authorized for every "
    "open network in range.";
}  // namespace

OpenAutoScanDisclaimerScreen& OpenAutoScanDisclaimerScreen::instance() {
    static OpenAutoScanDisclaimerScreen s;
    return s;
}

void OpenAutoScanDisclaimerScreen::onEnter() {}

void OpenAutoScanDisclaimerScreen::onKey(UiKey key, char ch) {
    if (key == UiKey::Char && (ch == 'y' || ch == 'Y')) {
        // Persisted (user's choice - see AppConfig::wardriveAutoScanOpen):
        // stays on across reboots until explicitly turned off.
        g_config.wardriveAutoScanOpen = true;
        g_config.wardriveAutoScanOpenAcknowledged = true;
        g_config.save();
        g_ui.popScreen();  // back to the WAR DRIVING screen
    } else if (key == UiKey::Back) {
        g_ui.popScreen();  // declined - enable nothing
    }
}

void OpenAutoScanDisclaimerScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);

    chrome::drawAlertHeader(gfx, "AUTO-SCAN OPEN NETWORKS");

    gfx.setTextColor(theme::AMBER, theme::BG);
    drawWrapped(gfx, kDisclaimer, 6, 20, 9, 37);

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, 110);
    gfx.print("Y: I am authorized");
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, 122);
    gfx.print("DEL: cancel");
}
