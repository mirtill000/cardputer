#include "CredDisclaimerScreen.h"
#include "CredAuditScreen.h"
#include "../TextWrap.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../../core/Config.h"

namespace {
// Deliberately blunt, and deliberately requires typing 'Y' rather than
// just hitting Enter (which the user is already reflexively pressing to
// move through every other screen in this app) — the one gate in the
// whole firmware that a muscle-memory keypress must not be able to
// clear by accident.
const char* kDisclaimer =
    "Tries username/password combos (built-in defaults + your own "
    "wordlists) against HTTP/Telnet/FTP login, rate-limited. This is a "
    "real attack tool now, not just a defaults check. Use ONLY on "
    "networks/devices you own or are explicitly authorized to test - "
    "unauthorized use is illegal.";
}  // namespace

CredDisclaimerScreen& CredDisclaimerScreen::instance() {
    static CredDisclaimerScreen s;
    return s;
}

void CredDisclaimerScreen::onKey(UiKey key, char ch) {
    if (key == UiKey::Char && (ch == 'y' || ch == 'Y')) {
        g_config.credAuditEnabled = true;
        g_config.credAuditAcknowledged = true;
        g_config.save();

        CredAuditScreen::instance().setTarget(_pendingTarget);
        g_ui.replaceScreen(&CredAuditScreen::instance());
        return;
    }
    if (key == UiKey::Back) {
        g_ui.popScreen();
    }
}

void CredDisclaimerScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);

    gfx.setTextColor(theme::RED, theme::BG);
    gfx.setCursor(4, 4);
    gfx.print(">> AUTHORIZATION REQUIRED");
    gfx.drawFastHLine(4, 15, gfx.width() - 8, theme::GREY);

    gfx.setTextColor(theme::AMBER, theme::BG);
    drawWrapped(gfx, kDisclaimer, 6, 20, 10, 37);

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, 110);
    gfx.print("Y: I own/am authorized");
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, 122);
    gfx.print("DEL: cancel");
}
