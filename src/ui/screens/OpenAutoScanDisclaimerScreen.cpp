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

void OpenAutoScanDisclaimerScreen::onEnter() {
    _typed = "";
    g_ui.setTextEntryMode(true);
}

void OpenAutoScanDisclaimerScreen::onExit() {
    g_ui.setTextEntryMode(false);
}

void OpenAutoScanDisclaimerScreen::onKey(UiKey key, char ch) {
    if (key == UiKey::Char) {
        if (_typed.length() < 20) _typed += ch;
    } else if (key == UiKey::Back) {
        if (_typed.length() > 0) {
            _typed.remove(_typed.length() - 1);
        } else {
            g_ui.popScreen();  // declined - enable nothing
        }
    } else if (key == UiKey::Enter) {
        if (_typed.equalsIgnoreCase("AUTHORIZED")) {
            // Session-only: NOT saved to NVS, so it resets to off on the
            // next boot. Only the "seen it once" acknowledgement persists.
            g_config.wardriveAutoScanOpen = true;
            g_config.wardriveAutoScanOpenAcknowledged = true;
            g_config.save();
            g_ui.popScreen();  // back to the WAR DRIVING screen
        } else {
            _typed = "";  // wrong word typed - clear and let them retry
        }
    }
}

void OpenAutoScanDisclaimerScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);

    chrome::drawAlertHeader(gfx, "AUTO-SCAN OPEN NETWORKS");

    gfx.setTextColor(theme::AMBER, theme::BG);
    drawWrapped(gfx, kDisclaimer, 6, 20, 9, 37);

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, 104);
    gfx.print("Type AUTHORIZED, then ENTER:");

    gfx.fillRect(6, 114, gfx.width() - 12, 10, theme::PANEL_BG);
    gfx.setTextColor(theme::GREEN_BRIGHT, theme::PANEL_BG);
    gfx.setCursor(8, 115);
    gfx.print(_typed);
}
