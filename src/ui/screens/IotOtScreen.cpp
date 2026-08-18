#include "IotOtScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/IotOtProbe.h"

IotOtScreen& IotOtScreen::instance() {
    static IotOtScreen s;
    return s;
}

void IotOtScreen::onEnter() {
    _selected = 0;
}

void IotOtScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // list pulled live from the probe each draw
}

void IotOtScreen::onKey(UiKey key, char ch) {
    if (_showDetail) {
        _showDetail = false;
        return;
    }
    if (key == UiKey::Enter) {
        if (!g_iotOtProbe.isRunning()) g_iotOtProbe.start();
    } else if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < g_iotOtProbe.count()) _selected++;
    } else if (key == UiKey::Char && (ch == 'i' || ch == 'I')) {
        if (g_iotOtProbe.count() > 0) _showDetail = true;
    } else if (key == UiKey::Back) {
        g_ui.popScreen();
    }
}

void IotOtScreen::draw(M5Canvas& gfx) {
    if (_showDetail) {
        IotOtProbe::Finding f;
        if (g_iotOtProbe.get(_selected, f)) {
            String text = f.service + " @ " + f.ip.toString() + (f.noAuth ? " (NO-AUTH): " : ": ") + f.detail;
            chrome::drawDetailOverlay(gfx, "IOT/OT FINDING", text);
        }
        return;
    }

    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "IOT/OT SWEEP");

    bool running = g_iotOtProbe.isRunning();
    gfx.setTextColor(running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("findings: ");
    gfx.print((unsigned)g_iotOtProbe.count());
    if (running) {
        gfx.print("  ");
        gfx.print((unsigned)g_iotOtProbe.progressPct());
        gfx.print("%");
    }

    drawFindings(gfx, 30);

    if (g_iotOtProbe.count() == 0 && !running) {
        chrome::drawEmptyState(gfx, "no exposed IoT/OT found", "ENTER: sweep the LAN (needs a scan)");
    }

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(running ? "probing hosts..." : "ENTER: sweep (mqtt/modbus/...)");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print(g_iotOtProbe.count() > 0 ? "I:full detail  DEL:back" : "DEL:back  (needs NETWORK SCAN first)");
}

void IotOtScreen::drawFindings(M5Canvas& gfx, int16_t top) {
    size_t count = g_iotOtProbe.count();
    if (count == 0) return;

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 4;  // two lines per finding

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    IotOtProbe::Finding f;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_iotOtProbe.get(i, f)) continue;

        int16_t y = top + 2 + (int16_t)row * (kRowH * 2);
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH * 2 - 2, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : (f.noAuth ? theme::RED : theme::AMBER), rowBg);
        gfx.setCursor(6, y);
        gfx.print(f.service);
        gfx.setCursor(120, y);
        gfx.print(f.ip.toString());

        gfx.setTextColor(theme::GREY, rowBg);
        gfx.setCursor(6, y + kRowH);
        String d = (f.noAuth ? String("NO-AUTH  ") : String("")) + f.detail;
        if (d.length() > 38) d = d.substring(0, 38);
        gfx.print(d);
    }

    chrome::drawScrollMarkers(gfx, top + 2, top + 2 + (int16_t)kMaxRows * (kRowH * 2), first > 0,
                               (first + kMaxRows) < count);
}
