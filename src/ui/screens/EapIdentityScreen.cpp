#include "EapIdentityScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Types.h"
#include "../../scan/EapIdentityHarvester.h"

EapIdentityScreen& EapIdentityScreen::instance() {
    static EapIdentityScreen s;
    return s;
}

void EapIdentityScreen::onEnter() {
    _running = g_eapIdentityHarvester.isRunning();
    _showDetail = false;
}

void EapIdentityScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // list is pulled live from the manager each draw
}

void EapIdentityScreen::onKey(UiKey key, char ch) {
    if (_showDetail) {
        _showDetail = false;
        return;
    }
    if (key == UiKey::Char && (ch == 'i' || ch == 'I')) {
        if (g_eapIdentityHarvester.count() > 0) _showDetail = true;
        return;
    }
    if (key == UiKey::Enter) {
        if (_running) {
            g_eapIdentityHarvester.stop();
        } else {
            g_eapIdentityHarvester.start();
        }
        _running = !_running;
    } else if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < g_eapIdentityHarvester.count()) _selected++;
    } else if (key == UiKey::Back) {
        g_ui.popScreen();  // keeps running in the background
    }
}

void EapIdentityScreen::draw(M5Canvas& gfx) {
    if (_showDetail) {
        EapIdentityHarvester::Sighting s;
        if (g_eapIdentityHarvester.get(_selected, s)) {
            String text = "identity: " + s.identity + " / supplicant MAC: " + macToString(s.mac) + " / seen " +
                          String(s.count) + "x";
            chrome::drawDetailOverlay(gfx, "EAP IDENTITY DETAIL", text);
        }
        return;
    }

    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "EAP IDENTITY");

    gfx.setTextColor(_running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("harvested: ");
    gfx.print((unsigned)g_eapIdentityHarvester.count());
    gfx.print(_running ? "  [listening]" : "");

    drawRows(gfx, 30);

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(_running ? "ENTER: stop" : "ENTER: start passive listen");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("I:detail  DEL:back  (STA channel)");
}

void EapIdentityScreen::drawRows(M5Canvas& gfx, int16_t top) {
    size_t count = g_eapIdentityHarvester.count();
    if (count == 0) {
        chrome::drawEmptyState(gfx, "no EAP identities yet", _running ? "listening..." : "press ENTER to start");
        return;
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 7;

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    EapIdentityHarvester::Sighting s;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_eapIdentityHarvester.get(i, s)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
        gfx.setCursor(6, y);
        String id = s.identity;
        if (id.length() > 30) id = id.substring(0, 30);
        gfx.print(id);
    }

    chrome::drawScrollMarkers(gfx, top + 2, top + 2 + (int16_t)kMaxRows * kRowH, first > 0, (first + kMaxRows) < count);
}
