#include "MainMenuScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../net/WifiManager.h"
#include <cstdio>
#include <cstring>

MainMenuScreen& MainMenuScreen::instance() {
    static MainMenuScreen s;
    return s;
}

void MainMenuScreen::configure(const MenuItem* items, uint8_t count) {
    _items = items;
    _count = count;
}

void MainMenuScreen::onEnter() {
    if (_selected >= _count) _selected = 0;
}

void MainMenuScreen::onKey(UiKey key, char /*ch*/) {
    if (_count == 0) return;
    switch (key) {
        case UiKey::Up:
            _selected = (_selected == 0) ? (uint8_t)(_count - 1) : (uint8_t)(_selected - 1);
            break;
        case UiKey::Down:
            _selected = (uint8_t)((_selected + 1) % _count);
            break;
        case UiKey::Enter:
            if (_items[_selected].target) g_ui.pushScreen(_items[_selected].target);
            break;
        default:
            break;
    }
}

void MainMenuScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "NETRUNNER");

    constexpr int16_t kRowH = 16;
    constexpr int16_t kTop = 22;

    for (uint8_t i = 0; i < _count; i++) {
        int16_t y = kTop + i * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        gfx.drawRect(4, y, gfx.width() - 8, kRowH - 2, sel ? theme::CYAN : theme::GREY);
        gfx.fillRect(5, y + 1, gfx.width() - 10, kRowH - 4, rowBg);

        gfx.setTextColor(sel ? theme::MAGENTA : theme::GREY, rowBg);
        gfx.setCursor(10, y + 2);
        gfx.print(sel ? "> " : "  ");
        gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
        gfx.print(_items[i].label);
    }

    // Bottom status bar: uptime (not wall-clock — no RTC/NTP wired up,
    // see README) / READY. / current IP. Replaces the old key-hint
    // footer on this one screen — Up/Down/Enter is already the
    // established convention by the time a user reaches the menu.
    uint32_t upSec = millis() / 1000;
    char upBuf[16];  // "HH:MM:SS" is 9 bytes, but hours isn't clamped - room for a much longer uptime
    snprintf(upBuf, sizeof(upBuf), "%02u:%02u:%02u", (unsigned)(upSec / 3600), (unsigned)((upSec / 60) % 60),
             (unsigned)(upSec % 60));

    int16_t statusY = gfx.height() - 9;
    gfx.setTextColor(theme::CYAN, theme::BG);
    gfx.setCursor(4, statusY);
    gfx.print(upBuf);

    const char* ready = "READY.";
    int16_t readyX = (gfx.width() - (int16_t)strlen(ready) * theme::GLYPH_W) / 2;
    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.setCursor(readyX, statusY);
    gfx.print(ready);

    String ipStr = g_wifi.isConnected() ? g_wifi.localIP().toString() : String("no ip");
    int16_t ipX = gfx.width() - (int16_t)ipStr.length() * theme::GLYPH_W - 4;
    gfx.setTextColor(theme::CYAN, theme::BG);
    gfx.setCursor(ipX, statusY);
    gfx.print(ipStr);
}
