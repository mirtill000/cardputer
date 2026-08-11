#include "BleScanScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/BleScanManager.h"

BleScanScreen& BleScanScreen::instance() {
    static BleScanScreen s;
    return s;
}

void BleScanScreen::onEnter() {
    // Reflects actual manager state, same reasoning as WardrivingScreen -
    // the manager keeps running in the background across screen visits.
    _state = g_bleScanManager.isRunning() ? State::Running : State::Idle;
    _logCount = 0;
}

void BleScanScreen::pushLog(const String& line) {
    if (_logCount < kLogLines) {
        _log[_logCount++] = line;
    } else {
        for (uint8_t i = 1; i < kLogLines; i++) _log[i - 1] = _log[i];
        _log[kLogLines - 1] = line;
    }
}

void BleScanScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::Ble) return;  // not ours — see ScanSource in EventQueue.h
    if (ev.type == ScanEventType::LogLine) pushLog(String(ev.text));
}

void BleScanScreen::onKey(UiKey key, char /*ch*/) {
    switch (_state) {
        case State::Idle:
            if (key == UiKey::Enter) {
                g_bleScanManager.start();
                _logCount = 0;
                _state = State::Running;
            } else if (key == UiKey::Up) {
                if (_sightingsSelected > 0) _sightingsSelected--;
            } else if (key == UiKey::Down) {
                if (_sightingsSelected + 1 < g_bleScanManager.sightingCount()) _sightingsSelected++;
            } else if (key == UiKey::Back) {
                g_ui.popScreen();
            }
            break;

        case State::Running:
            if (key == UiKey::Enter) {
                g_bleScanManager.stop();
                _state = State::Idle;
            } else if (key == UiKey::Back) {
                g_ui.popScreen();  // keeps running in the background - see BleScanManager
            }
            break;
    }
}

void BleScanScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "BLE SCAN");

    switch (_state) {
        case State::Idle: {
            gfx.setTextColor(theme::GREEN, theme::BG);
            gfx.setCursor(6, 18);
            gfx.print("seen:");
            gfx.print((unsigned)g_bleScanManager.sightingCount());

            drawSightings(gfx, 30);

            gfx.setTextColor(theme::MAGENTA, theme::BG);
            gfx.setCursor(6, gfx.height() - 20);
            gfx.print("ENTER: start passive scan");

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("DEL:back");
            break;
        }

        case State::Running: {
            gfx.setTextColor(theme::CYAN, theme::BG);
            gfx.setCursor(6, 18);
            gfx.print("seen:");
            gfx.print((unsigned)g_bleScanManager.sightingCount());

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.drawFastHLine(4, 29, gfx.width() - 8, theme::GREY);

            for (uint8_t i = 0; i < _logCount; i++) {
                int16_t y = 32 + i * 9;
                gfx.setTextColor(theme::GREEN, theme::BG);
                gfx.setCursor(6, y);
                String line = _log[i];
                if (line.length() > 37) line = line.substring(0, 37);
                gfx.print(line);
            }

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("ENTER:stop DEL:back(keeps running)");
            break;
        }
    }
}

void BleScanScreen::drawSightings(M5Canvas& gfx, int16_t top) {
    size_t count = g_bleScanManager.sightingCount();
    if (count == 0) return;

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 8;

    size_t first = 0;
    if (_sightingsSelected >= kMaxRows) first = _sightingsSelected - kMaxRows + 1;

    BleScanManager::BleSighting d;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_bleScanManager.getSighting(i, d)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _sightingsSelected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
        gfx.setCursor(6, y);

        String label = d.name.length() ? d.name : d.address;
        if (label.length() > 24) label = label.substring(0, 24);
        gfx.print(label);

        gfx.setCursor(200, y);
        gfx.print(d.rssi);
    }
}
