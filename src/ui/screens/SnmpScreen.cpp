#include "SnmpScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/SnmpSweep.h"

SnmpScreen& SnmpScreen::instance() {
    static SnmpScreen s;
    return s;
}

void SnmpScreen::onEnter() {
    _selected = 0;
}

void SnmpScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // list pulled live from the sweep each draw
}

void SnmpScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Enter) {
        if (!g_snmpSweep.isRunning()) g_snmpSweep.start();
    } else if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < g_snmpSweep.count()) _selected++;
    } else if (key == UiKey::Back) {
        g_ui.popScreen();
    }
}

void SnmpScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "SNMP SWEEP");

    bool running = g_snmpSweep.isRunning();
    gfx.setTextColor(running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("public: ");
    gfx.print((unsigned)g_snmpSweep.count());
    if (running) {
        gfx.print("  ");
        gfx.print((unsigned)g_snmpSweep.progressPct());
        gfx.print("%");
    }

    drawResponders(gfx, 30);

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(running ? "sweeping alive hosts..." : "ENTER: sweep (community=public)");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back  (needs NETWORK SCAN first)");
}

void SnmpScreen::drawResponders(M5Canvas& gfx, int16_t top) {
    size_t count = g_snmpSweep.count();
    if (count == 0) return;

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 4;  // two lines per responder

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    SnmpSweep::Responder r;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_snmpSweep.get(i, r)) continue;

        int16_t y = top + 2 + (int16_t)row * (kRowH * 2);
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH * 2 - 2, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : theme::AMBER, rowBg);
        gfx.setCursor(6, y);
        gfx.print(r.ip.toString());

        gfx.setTextColor(theme::GREY, rowBg);
        gfx.setCursor(6, y + kRowH);
        String d = r.sysDescr;
        if (d.length() > 38) d = d.substring(0, 38);
        gfx.print(d);
    }
}
