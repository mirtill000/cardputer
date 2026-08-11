#include "PlaceholderScreen.h"
#include "../TextWrap.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"

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
    chrome::drawHeader(gfx, _title);

    gfx.setTextColor(theme::GREEN, theme::BG);
    drawWrapped(gfx, _description, 6, 26, 10, 37);

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back");
}
