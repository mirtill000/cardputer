#include "RogueDhcpScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/RogueDhcpDetector.h"

RogueDhcpScreen& RogueDhcpScreen::instance() {
    static RogueDhcpScreen s;
    return s;
}

void RogueDhcpScreen::onEnter() {
    _running = g_rogueDhcpDetector.isRunning();
}

void RogueDhcpScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // the list is pulled live from the detector on each draw - no per-line log kept here
}

void RogueDhcpScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Enter) {
        if (_running) {
            g_rogueDhcpDetector.stop();
        } else {
            g_rogueDhcpDetector.start();
        }
        _running = !_running;
    } else if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < g_rogueDhcpDetector.sightingCount()) _selected++;
    } else if (key == UiKey::Back) {
        g_ui.popScreen();  // keeps running in the background - see RogueDhcpDetector
    }
}

void RogueDhcpScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "ROGUE DHCP");

    gfx.setTextColor(_running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("servers: ");
    gfx.print((unsigned)g_rogueDhcpDetector.sightingCount());
    gfx.print(_running ? "  [watching]" : "");

    drawSightings(gfx, 30);

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(_running ? "ENTER: stop" : "ENTER: start passive watch");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back  (red = unexpected server)");
}

void RogueDhcpScreen::drawSightings(M5Canvas& gfx, int16_t top) {
    size_t count = g_rogueDhcpDetector.sightingCount();
    if (count == 0) return;

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 8;

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    RogueDhcpDetector::Sighting s;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_rogueDhcpDetector.getSighting(i, s)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        uint16_t fg = sel ? theme::CYAN : (s.suspicious ? theme::RED : theme::GREEN);
        gfx.setTextColor(fg, rowBg);
        gfx.setCursor(6, y);
        gfx.print(s.serverIp.toString());

        gfx.setCursor(120, y);
        gfx.setTextColor(sel ? theme::CYAN : theme::GREY, rowBg);
        gfx.print("-> ");
        gfx.print(s.offeredIp.toString());
    }

    chrome::drawScrollMarkers(gfx, top + 2, top + 2 + (int16_t)kMaxRows * kRowH, first > 0, (first + kMaxRows) < count);
}
