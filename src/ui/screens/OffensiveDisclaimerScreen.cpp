#include "OffensiveDisclaimerScreen.h"
#include "../TextWrap.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Config.h"

namespace {
const char* kDisclaimer =
    "Affects OTHER devices - ARP spoofing, deauth, or a look-alike "
    "AP - not just your own. Use ONLY on networks/devices you own or "
    "are explicitly authorized to test. Intercepting others' traffic "
    "can implicate wiretapping law separately from network-owner "
    "authorization.";
}  // namespace

OffensiveDisclaimerScreen& OffensiveDisclaimerScreen::instance() {
    static OffensiveDisclaimerScreen s;
    return s;
}

void OffensiveDisclaimerScreen::onEnter() {
    _typed = "";
    g_ui.setTextEntryMode(true);
}

void OffensiveDisclaimerScreen::onExit() {
    g_ui.setTextEntryMode(false);
}

void OffensiveDisclaimerScreen::onKey(UiKey key, char ch) {
    if (key == UiKey::Char) {
        if (_typed.length() < 20) _typed += ch;
    } else if (key == UiKey::Back) {
        if (_typed.length() > 0) {
            _typed.remove(_typed.length() - 1);
        } else {
            g_ui.popScreen();
        }
    } else if (key == UiKey::Enter) {
        if (_typed.equalsIgnoreCase("AUTHORIZED")) {
            g_config.offensiveEnabled = true;
            g_config.offensiveAcknowledged = true;
            g_config.save();
            if (_pendingTarget) g_ui.replaceScreen(_pendingTarget);
        } else {
            _typed = "";  // wrong word typed - clear and let them retry
        }
    }
}

void OffensiveDisclaimerScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);

    chrome::drawAlertHeader(gfx, "ACTIVE OFFENSIVE TOOL");

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
