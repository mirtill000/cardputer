#include "PmkidSweepScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/PmkidSweepManager.h"
#include "../../scan/WardrivingManager.h"
#include "../../net/WifiManager.h"

PmkidSweepScreen& PmkidSweepScreen::instance() {
    static PmkidSweepScreen s;
    return s;
}

void PmkidSweepScreen::onEnter() {
    _selected = 0;
}

void PmkidSweepScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // the list is pulled live from the manager on each draw - no per-line log kept here
}

void PmkidSweepScreen::onKey(UiKey key, char ch) {
    if (_showDetail) {
        _showDetail = false;
        return;
    }
    if (key == UiKey::Char && (ch == 'i' || ch == 'I')) {
        if (g_pmkidSweepManager.resultCount() > 0) _showDetail = true;
        return;
    }
    switch (key) {
        case UiKey::Enter:
            if (g_pmkidSweepManager.isRunning()) {
                g_pmkidSweepManager.stop();
            } else {
                g_pmkidSweepManager.start();  // silently no-ops without WiFi/eligible targets - see the status line
            }
            break;
        case UiKey::Up:
            if (_selected > 0) _selected--;
            break;
        case UiKey::Down:
            if (_selected + 1 < g_pmkidSweepManager.resultCount()) _selected++;
            break;
        case UiKey::Back:
            g_ui.popScreen();  // keeps running in the background - see PmkidSweepManager
            break;
        default:
            break;
    }
}

void PmkidSweepScreen::draw(M5Canvas& gfx) {
    if (_showDetail) {
        PmkidSweepManager::SweepResult r;
        if (g_pmkidSweepManager.getResult(_selected, r)) {
            String text = "SSID: " + r.ssid + " / BSSID: " + r.bssid + " / PMKID: " +
                          (r.pmkidCaptured ? "likely captured" : "not seen") +
                          " / frames: " + String((unsigned)r.framesCaptured) + " / pcap: " + r.pcapPath;
            chrome::drawDetailOverlay(gfx, "SWEEP RESULT", text);
        }
        return;
    }

    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "PMKID SWEEP");

    bool running = g_pmkidSweepManager.isRunning();
    bool connected = g_wifi.isConnected();

    gfx.setTextColor(running ? theme::CYAN : (connected ? theme::GREEN : theme::AMBER), theme::BG);
    gfx.setCursor(6, 18);
    if (running) {
        gfx.print((unsigned)g_pmkidSweepManager.currentIndex());
        gfx.print("/");
        gfx.print((unsigned)g_pmkidSweepManager.targetCount());
        gfx.print(" targets, ");
        gfx.print((unsigned)g_pmkidSweepManager.hitCount());
        gfx.print(" hits");
    } else if (!connected) {
        gfx.print("connect to WiFi first");
    } else if (g_wardrivingManager.sightingCount() == 0) {
        gfx.print("no WAR DRIVING sightings yet");
    } else {
        gfx.print("not running");
    }

    drawResults(gfx, 28);

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(running ? "ENTER: stop" : "ENTER: start sweep");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print(g_pmkidSweepManager.resultCount() > 0 ? "I:detail  DEL:back" : "DEL:back");
}

void PmkidSweepScreen::drawResults(M5Canvas& gfx, int16_t top) {
    size_t count = g_pmkidSweepManager.resultCount();
    if (count == 0) {
        chrome::drawEmptyState(gfx, "no results yet",
                                g_pmkidSweepManager.isRunning() ? "sweeping..." : "press ENTER to start");
        return;
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 6;
    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    PmkidSweepManager::SweepResult r;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_pmkidSweepManager.getResult(i, r)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : (r.pmkidCaptured ? theme::GREEN : theme::GREY), rowBg);
        gfx.setCursor(6, y);
        String label = r.ssid;
        if (label.length() > 24) label = label.substring(0, 24);
        gfx.print(label);

        if (r.pmkidCaptured) {
            gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
            gfx.setCursor(200, y);
            gfx.print("HIT");
        }
    }

    chrome::drawScrollMarkers(gfx, top + 2, top + 2 + (int16_t)kMaxRows * kRowH, first > 0, (first + kMaxRows) < count);
}
