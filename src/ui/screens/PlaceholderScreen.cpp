#include "PlaceholderScreen.h"
#include "../TextWrap.h"
#include "../UiManager.h"
#include "../Theme.h"

void PlaceholderScreen::configure(const char* title, const char* description) {
    _title = title;
    _description = description;
}

void PlaceholderScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Back || key == UiKey::Enter) {
        g_ui.popScreen();
    }
}

void PlaceholderScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);

    gfx.setTextSize(1);
    gfx.setTextColor(theme::GREEN_BRIGHT, theme::BG);
    gfx.setCursor(6, 4);
    gfx.print(">> ");
    gfx.print(_title);
    gfx.drawFastHLine(4, 15, gfx.width() - 8, theme::GREY);

    gfx.setTextColor(theme::GREEN, theme::BG);
    drawWrapped(gfx, _description, 6, 26, 10, 37);

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back");
}
