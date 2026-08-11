#include "MainMenuScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../net/WifiManager.h"
#include <cstdio>
#include <cstring>

namespace {
// Static, deliberately not randomized: this redraws every frame (~30fps),
// so anything non-deterministic here would jitter instead of animate.
// Purely decorative — outline-only "neon skyline" silhouette, cheap to
// draw (a handful of drawRect calls, no fill).
struct Building {
    int16_t x, w, h;
    bool cyan;
};
constexpr Building kSkyline[] = {
    {2, 14, 14, true},  {18, 10, 20, false}, {30, 16, 10, true},  {48, 12, 24, false},
    {62, 18, 16, true}, {82, 10, 22, false}, {94, 14, 12, true},  {110, 20, 26, false},
    {132, 12, 14, true}, {146, 16, 20, false}, {164, 10, 12, true}, {176, 22, 24, false},
    {200, 14, 16, true}, {216, 18, 10, false},
};
constexpr int16_t kSkylineTop = 18;
constexpr int16_t kSkylineH = 24;
constexpr int16_t kSkylineBaseline = kSkylineTop + kSkylineH;
}  // namespace

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

    // Decorative skyline band between the header and the menu list.
    gfx.drawFastHLine(0, kSkylineBaseline, gfx.width(), theme::GREY);
    for (const auto& b : kSkyline) {
        gfx.drawRect(b.x, kSkylineBaseline - b.h, b.w, b.h, b.cyan ? theme::CYAN : theme::MAGENTA);
    }

    constexpr int16_t kRowH = 14;
    constexpr int16_t kTop = kSkylineBaseline + 4;

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
    char upBuf[10];
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
