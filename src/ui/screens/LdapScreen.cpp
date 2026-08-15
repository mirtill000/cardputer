#include "LdapScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/LdapProbe.h"

LdapScreen& LdapScreen::instance() {
    static LdapScreen s;
    return s;
}

void LdapScreen::onEnter() {
    _selected = 0;
}

void LdapScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // list pulled live from the probe each draw
}

void LdapScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Enter) {
        if (!g_ldapProbe.isRunning()) g_ldapProbe.start();
    } else if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < g_ldapProbe.count()) _selected++;
    } else if (key == UiKey::Back) {
        g_ui.popScreen();
    }
}

void LdapScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "LDAP SWEEP");

    bool running = g_ldapProbe.isRunning();
    gfx.setTextColor(running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("responders: ");
    gfx.print((unsigned)g_ldapProbe.count());
    if (running) {
        gfx.print("  ");
        gfx.print((unsigned)g_ldapProbe.progressPct());
        gfx.print("%");
    }

    drawFindings(gfx, 30);

    if (g_ldapProbe.count() == 0 && !running) {
        chrome::drawEmptyState(gfx, "no LDAP responders found", "ENTER: sweep the LAN (needs a scan)");
    }

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(running ? "probing hosts (port 389)..." : "ENTER: sweep (anon bind + rootDSE)");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back  (needs NETWORK SCAN first)");
}

void LdapScreen::drawFindings(M5Canvas& gfx, int16_t top) {
    size_t count = g_ldapProbe.count();
    if (count == 0) return;

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 4;  // two lines per finding

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    LdapProbe::Finding f;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_ldapProbe.get(i, f)) continue;

        int16_t y = top + 2 + (int16_t)row * (kRowH * 2);
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH * 2 - 2, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : (f.anonymousBindAllowed ? theme::RED : theme::GREEN), rowBg);
        gfx.setCursor(6, y);
        gfx.print(f.ip.toString());
        gfx.setCursor(120, y);
        gfx.print(f.anonymousBindAllowed ? "anon-bind OPEN" : "bind rejected");

        gfx.setTextColor(theme::GREY, rowBg);
        gfx.setCursor(6, y + kRowH);
        String detail = f.dnsHostName.length() ? f.dnsHostName : f.defaultNamingContext;
        if (!detail.length()) detail = "rootDSE not disclosed";
        if (detail.length() > 38) detail = detail.substring(0, 38);
        gfx.print(detail);
    }
}
