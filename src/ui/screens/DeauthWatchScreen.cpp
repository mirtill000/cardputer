#include "DeauthWatchScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/DeauthWatcher.h"

DeauthWatchScreen& DeauthWatchScreen::instance() {
    static DeauthWatchScreen s;
    return s;
}

void DeauthWatchScreen::onEnter() {
    _running = g_deauthWatcher.isRunning();
}

void DeauthWatchScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // the list is pulled live from the watcher on each draw - no per-line log kept here
}

void DeauthWatchScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Enter) {
        if (_running) {
            g_deauthWatcher.stop();
        } else {
            g_deauthWatcher.start();
        }
        _running = !_running;
    } else if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < g_deauthWatcher.incidentCount()) _selected++;
    } else if (key == UiKey::Back) {
        g_ui.popScreen();  // keeps running in the background - see DeauthWatcher
    }
}

void DeauthWatchScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "GUARD MODE");

    gfx.setTextColor(_running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("frames: ");
    gfx.print((unsigned)g_deauthWatcher.totalFrames());
    gfx.print(_running ? "  [watching]" : "");

    bool flooding = g_deauthWatcher.anyFlooding();
    if (flooding) {
        gfx.setTextColor(theme::RED, theme::BG);
        gfx.setCursor(6, 28);
        gfx.print("FLOOD DETECTED");
    } else if (!_running) {
        // SENTINEL MODE folds this exact detector in alongside its own
        // discovery + traffic dump - only worth running THIS screen
        // standalone if all you want is flood detection by itself.
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(6, 28);
        gfx.print("(SENTINEL MODE includes this)");
    }

    drawIncidents(gfx, flooding ? 38 : 30);

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(_running ? "ENTER: stop" : "ENTER: start passive watch");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back  t=total /w=this window");
}

void DeauthWatchScreen::drawIncidents(M5Canvas& gfx, int16_t top) {
    size_t count = g_deauthWatcher.incidentCount();
    if (count == 0) {
        chrome::drawEmptyState(gfx, "no deauth/disassoc seen", _running ? "watching..." : "press ENTER to start");
        return;
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 6;

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    DeauthWatcher::Incident inc;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_deauthWatcher.getIncident(i, inc)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        uint16_t fg = sel ? theme::CYAN : (inc.flooding ? theme::RED : theme::GREEN);
        gfx.setTextColor(fg, rowBg);
        gfx.setCursor(6, y);
        gfx.print(inc.bssid);

        gfx.setCursor(150, y);
        gfx.setTextColor(sel ? theme::CYAN : theme::GREY, rowBg);
        gfx.print((unsigned)inc.count);
        gfx.print("t");

        // Current 10s window count, separate from the running total -
        // this is the number that actually decides "flooding" (see
        // DeauthWatcher::kFloodThreshold), so it's worth seeing live
        // even before it's high enough to turn the row red.
        gfx.setCursor(195, y);
        gfx.setTextColor(sel ? theme::CYAN : (inc.flooding ? theme::RED : theme::GREY), rowBg);
        gfx.print((unsigned)inc.windowCount);
        gfx.print("/w");
    }

    chrome::drawScrollMarkers(gfx, top + 2, top + 2 + (int16_t)kMaxRows * kRowH, first > 0, (first + kMaxRows) < count);
}
