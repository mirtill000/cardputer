#include "SsdpScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/SsdpDiscovery.h"

SsdpScreen& SsdpScreen::instance() {
    static SsdpScreen s;
    return s;
}

void SsdpScreen::onEnter() {
    _selected = 0;
}

void SsdpScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // just triggers a redraw via the normal event->draw cycle - no per-line log needed here
}

void SsdpScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Enter) {
        if (!g_ssdpDiscovery.isRunning()) g_ssdpDiscovery.start();
    } else if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < g_ssdpDiscovery.deviceCount()) _selected++;
    } else if (key == UiKey::Back) {
        g_ui.popScreen();
    }
}

void SsdpScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "UPNP DISCOVERY");

    bool running = g_ssdpDiscovery.isRunning();
    gfx.setTextColor(running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("found: ");
    gfx.print((unsigned)g_ssdpDiscovery.deviceCount());
    gfx.print(running ? "  [searching...]" : "");

    drawDevices(gfx, 30);

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(running ? "waiting for replies..." : "ENTER: search (M-SEARCH)");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back");
}

void SsdpScreen::drawDevices(M5Canvas& gfx, int16_t top) {
    size_t count = g_ssdpDiscovery.deviceCount();
    if (count == 0) return;

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 4;  // two lines per device below, so fewer rows fit

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    SsdpDiscovery::Device d;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_ssdpDiscovery.getDevice(i, d)) continue;

        int16_t y = top + 2 + (int16_t)row * (kRowH * 2);
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH * 2 - 2, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
        gfx.setCursor(6, y);
        gfx.print(d.fromIp.toString());
        gfx.setCursor(80, y);
        String server = d.server.length() ? d.server : String("(no SERVER header)");
        if (server.length() > 25) server = server.substring(0, 25);
        gfx.print(server);

        gfx.setTextColor(theme::GREY, rowBg);
        gfx.setCursor(6, y + kRowH);
        String usn = d.usn;
        if (usn.length() > 37) usn = usn.substring(0, 37);
        gfx.print(usn);
    }
}
