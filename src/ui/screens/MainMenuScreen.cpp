#include "MainMenuScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"

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
    chrome::drawHeader(gfx, "MAIN MENU");

    constexpr int16_t kRowH = 16;
    constexpr int16_t kTop = 22;

    for (uint8_t i = 0; i < _count; i++) {
        int16_t y = kTop + i * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        gfx.drawRect(4, y, gfx.width() - 8, kRowH - 2, sel ? theme::CYAN : theme::GREY);
        gfx.fillRect(5, y + 1, gfx.width() - 10, kRowH - 4, rowBg);

        gfx.setTextColor(sel ? theme::MAGENTA : theme::GREY, rowBg);
        gfx.setCursor(10, y + 3);
        gfx.print(sel ? "> " : "  ");
        gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
        gfx.print(_items[i].label);
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("ENTER:select  ;/. :nav");
}
