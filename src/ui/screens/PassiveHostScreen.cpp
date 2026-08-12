#include "PassiveHostScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Types.h"
#include "../../scan/PassiveHostDiscovery.h"

PassiveHostScreen& PassiveHostScreen::instance() {
    static PassiveHostScreen s;
    return s;
}

void PassiveHostScreen::onEnter() {
    _running = g_passiveHostDiscovery.isRunning();
}

void PassiveHostScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // list is pulled live from the manager each draw
}

void PassiveHostScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Enter) {
        if (_running) {
            g_passiveHostDiscovery.stop();
        } else {
            g_passiveHostDiscovery.start();
        }
        _running = !_running;
    } else if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < g_passiveHostDiscovery.count()) _selected++;
    } else if (key == UiKey::Back) {
        g_ui.popScreen();  // keeps running in the background
    }
}

void PassiveHostScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "PASSIVE HOSTS");

    gfx.setTextColor(_running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("overheard: ");
    gfx.print((unsigned)g_passiveHostDiscovery.count());
    gfx.print(_running ? "  [listening]" : "");

    drawHosts(gfx, 30);

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(_running ? "ENTER: stop" : "ENTER: start passive listen");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back  (open networks only)");
}

void PassiveHostScreen::drawHosts(M5Canvas& gfx, int16_t top) {
    size_t count = g_passiveHostDiscovery.count();
    if (count == 0) return;

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 8;

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    PassiveHostDiscovery::Observed o;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_passiveHostDiscovery.get(i, o)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
        gfx.setCursor(6, y);
        gfx.print(o.ip.toString());

        gfx.setTextColor(sel ? theme::CYAN : theme::GREY, rowBg);
        gfx.setCursor(110, y);
        gfx.print(o.macKnown ? macToString(o.mac) : String("--"));

        gfx.setCursor(210, y);
        gfx.print((unsigned)o.frames);
    }
}
