#include "MainMenuScreen.h"
#include "../UiManager.h"
#include "../Theme.h"

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
    gfx.setTextSize(1);
    gfx.setTextColor(theme::GREEN_BRIGHT, theme::BG);
    gfx.setCursor(6, 4);
    gfx.print(">> MAIN MENU");
    gfx.drawFastHLine(4, 15, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 16;
    constexpr int16_t kTop = 22;

    for (uint8_t i = 0; i < _count; i++) {
        int16_t y = kTop + i * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::GREEN_DIM : theme::BG;
        gfx.fillRect(4, y, gfx.width() - 8, kRowH - 2, rowBg);
        gfx.setTextColor(sel ? theme::GREEN_BRIGHT : theme::GREEN, rowBg);
        gfx.setCursor(10, y + 3);
        gfx.print(sel ? "> " : "  ");
        gfx.print(_items[i].label);
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("ENTER:select  ;/. :nav");
}
