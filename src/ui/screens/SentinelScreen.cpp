#include "SentinelScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/SentinelManager.h"
#include "../../net/WifiManager.h"

SentinelScreen& SentinelScreen::instance() {
    static SentinelScreen s;
    return s;
}

void SentinelScreen::onEnter() {
    _selected = 0;
}

void SentinelScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // the list is pulled live from the manager on each draw - no per-line log kept here
}

void SentinelScreen::onKey(UiKey key, char ch) {
    if (_showDetail) {
        _showDetail = false;
        return;
    }
    if (key == UiKey::Char && (ch == 'i' || ch == 'I')) {
        if (g_sentinelManager.newDeviceLogCount() > 0) _showDetail = true;
        return;
    }
    switch (key) {
        case UiKey::Enter:
            if (g_sentinelManager.isRunning()) {
                g_sentinelManager.stop();
            } else {
                g_sentinelManager.start();  // silently no-ops without WiFi - see the status line
            }
            break;
        case UiKey::Up:
            if (_selected > 0) _selected--;
            break;
        case UiKey::Down:
            if (_selected + 1 < g_sentinelManager.newDeviceLogCount()) _selected++;
            break;
        case UiKey::Back:
            g_ui.popScreen();  // keeps running in the background - see SentinelManager
            break;
        default:
            break;
    }
}

void SentinelScreen::draw(M5Canvas& gfx) {
    if (_showDetail) {
        SentinelManager::NewDevice d;
        if (g_sentinelManager.getNewDevice(_selected, d)) {
            String text = "IP: " + d.ip.toString() + " / MAC: " + d.mac + " / host: " +
                          (d.hostname.length() ? d.hostname : String("?")) + " / vendor: " +
                          (d.vendor.length() ? d.vendor : String("unknown"));
            chrome::drawDetailOverlay(gfx, "NEW DEVICE", text);
        }
        return;
    }

    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "SENTINEL MODE");

    bool running = g_sentinelManager.isRunning();
    bool connected = g_wifi.isConnected();

    gfx.setTextColor(running ? theme::CYAN : (connected ? theme::GREEN : theme::AMBER), theme::BG);
    gfx.setCursor(6, 18);
    if (running) {
        gfx.print("net: ");
        String net = g_sentinelManager.network();
        if (net.length() > 24) net = net.substring(0, 24);
        gfx.print(net);
    } else if (!connected) {
        gfx.print("connect to WiFi first");
    } else {
        gfx.print("not watching");
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, 28);
    gfx.print("new: ");
    gfx.print((unsigned)g_sentinelManager.newDeviceCount());
    gfx.print("  frames: ");
    gfx.print((unsigned)g_sentinelManager.capturedFrames());
    gfx.print("  cycles: ");
    gfx.print((unsigned)g_sentinelManager.cyclesRun());

    drawDevices(gfx, 38);

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(running ? "ENTER: stop" : "ENTER: start (needs WiFi)");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print(g_sentinelManager.newDeviceLogCount() > 0 ? "I:detail  DEL:back" : "DEL:back");
}

void SentinelScreen::drawDevices(M5Canvas& gfx, int16_t top) {
    size_t count = g_sentinelManager.newDeviceLogCount();
    if (count == 0) {
        chrome::drawEmptyState(gfx, "no new devices yet",
                                g_sentinelManager.isRunning() ? "watching..." : "press ENTER to start");
        return;
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 6;
    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    SentinelManager::NewDevice d;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_sentinelManager.getNewDevice(i, d)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : theme::MAGENTA, rowBg);
        gfx.setCursor(6, y);
        gfx.print(d.ip.toString());

        gfx.setTextColor(sel ? theme::CYAN : theme::GREY, rowBg);
        gfx.setCursor(90, y);
        String label = d.hostname.length() ? d.hostname : d.vendor;
        if (label.length() > 24) label = label.substring(0, 24);
        gfx.print(label);
    }

    chrome::drawScrollMarkers(gfx, top + 2, top + 2 + (int16_t)kMaxRows * kRowH, first > 0, (first + kMaxRows) < count);
}
