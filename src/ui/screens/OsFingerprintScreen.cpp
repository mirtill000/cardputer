#include "OsFingerprintScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Types.h"
#include "../../scan/OsFingerprint.h"

OsFingerprintScreen& OsFingerprintScreen::instance() {
    static OsFingerprintScreen s;
    return s;
}

void OsFingerprintScreen::onEnter() {
    _running = g_osFingerprint.isRunning();
    _showDetail = false;
}

void OsFingerprintScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // list is pulled live from the manager each draw
}

void OsFingerprintScreen::onKey(UiKey key, char ch) {
    if (_showDetail) {
        _showDetail = false;
        return;
    }
    if (key == UiKey::Char && (ch == 'i' || ch == 'I')) {
        if (g_osFingerprint.count() > 0) _showDetail = true;
        return;
    }
    if (key == UiKey::Enter) {
        if (_running) {
            g_osFingerprint.stop();
        } else {
            g_osFingerprint.start();
        }
        _running = !_running;
    } else if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < g_osFingerprint.count()) _selected++;
    } else if (key == UiKey::Back) {
        g_ui.popScreen();  // keeps running in the background
    }
}

void OsFingerprintScreen::draw(M5Canvas& gfx) {
    if (_showDetail) {
        OsFingerprint::Sighting s;
        if (g_osFingerprint.get(_selected, s)) {
            String text = "IP: " + s.ip.toString() + " / TTL: " + String(s.ttl) + " (guess: " +
                          OsFingerprint::ttlGuessLabel(s.ttlCeil) + ") / window: " + String(s.window) +
                          " / TCP options seen: " + (s.optionOrder.length() ? s.optionOrder : String("none")) +
                          " (M=MSS W=WScale S=SACK T=Timestamp N=NOP)";
            chrome::drawDetailOverlay(gfx, "OS FINGERPRINT DETAIL", text);
        }
        return;
    }

    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "OS FINGERPRINT");

    gfx.setTextColor(_running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("seen: ");
    gfx.print((unsigned)g_osFingerprint.count());
    gfx.print(_running ? "  [listening]" : "");

    drawHosts(gfx, 30);

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(_running ? "ENTER: stop" : "ENTER: start passive listen");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("I:detail  DEL:back  (open nets only)");
}

void OsFingerprintScreen::drawHosts(M5Canvas& gfx, int16_t top) {
    size_t count = g_osFingerprint.count();
    if (count == 0) {
        chrome::drawEmptyState(gfx, "no SYN-ACK seen yet", _running ? "listening..." : "press ENTER to start");
        return;
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 7;

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    OsFingerprint::Sighting s;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_osFingerprint.get(i, s)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
        gfx.setCursor(6, y);
        gfx.print(s.ip.toString());

        gfx.setTextColor(sel ? theme::CYAN : theme::AMBER, rowBg);
        gfx.setCursor(110, y);
        gfx.print("t");
        gfx.print(s.ttlCeil);

        gfx.setTextColor(sel ? theme::CYAN : theme::GREY, rowBg);
        gfx.setCursor(140, y);
        gfx.print(s.optionOrder);
    }

    chrome::drawScrollMarkers(gfx, top + 2, top + 2 + (int16_t)kMaxRows * kRowH, first > 0, (first + kMaxRows) < count);
}
