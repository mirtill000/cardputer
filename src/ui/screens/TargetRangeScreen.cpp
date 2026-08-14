#include "TargetRangeScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/ScanManager.h"
#include <IPAddress.h>

TargetRangeScreen& TargetRangeScreen::instance() {
    static TargetRangeScreen s;
    return s;
}

void TargetRangeScreen::onEnter() {
    _input = "";
    _status = "";
    g_ui.setTextEntryMode(true);  // need literal '.' for the IP
}

void TargetRangeScreen::onExit() {
    g_ui.setTextEntryMode(false);
}

void TargetRangeScreen::onKey(UiKey key, char ch) {
    if (key == UiKey::Char) {
        if (ch == 'c' || ch == 'C') {
            g_scanManager.clearScanRange();
            _status = "cleared - using DHCP subnet";
            _input = "";
            return;
        }
        if (_input.length() < 15) _input += ch;
    } else if (key == UiKey::Back) {
        if (_input.length() > 0) {
            _input.remove(_input.length() - 1);
        } else {
            g_ui.popScreen();
        }
    } else if (key == UiKey::Enter) {
        IPAddress base;
        if (base.fromString(_input)) {
            g_scanManager.setScanRange(base, 254);  // scan base+1 .. base+254 (a /24)
            _status = String("set: ") + base.toString() + "/24";
        } else {
            _status = "invalid IP";
        }
    }
}

void TargetRangeScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "TARGET RANGE");

    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.setCursor(6, 20);
    gfx.print("current: ");
    gfx.setTextColor(theme::CYAN, theme::BG);
    if (g_scanManager.hasCustomRange()) {
        gfx.print(g_scanManager.customBase().toString());
        gfx.print("/24");
    } else {
        gfx.print("DHCP subnet");
    }

    gfx.setTextColor(theme::CYAN, theme::BG);
    gfx.setCursor(6, 36);
    gfx.print("base IP:");
    gfx.fillRect(6, 46, gfx.width() - 12, 10, theme::PANEL_BG);
    gfx.setTextColor(theme::GREEN_BRIGHT, theme::PANEL_BG);
    gfx.setCursor(8, 47);
    gfx.print(_input);
    if ((millis() / 500) % 2 == 0) {
        gfx.setCursor(8 + (int16_t)_input.length() * theme::GLYPH_W, 47);
        gfx.print("_");
    }

    if (_status.length()) {
        gfx.setTextColor(theme::MAGENTA, theme::BG);
        gfx.setCursor(6, 64);
        gfx.print(_status);
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, 88);
    gfx.print("note: other subnets need routing;");
    gfx.setCursor(6, 98);
    gfx.print("MAC/vendor only on local subnet.");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("ENTER:set C:clear DEL:erase/back");
}
