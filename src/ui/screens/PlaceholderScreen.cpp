#include "PlaceholderScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include <cstring>

void PlaceholderScreen::configure(const char* title, const char* description) {
    _title = title;
    _description = description;
}

void PlaceholderScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Back || key == UiKey::Enter) {
        g_ui.popScreen();
    }
}

namespace {
// Minimal greedy word-wrap for the fixed-width 6px font: prints
// `text` starting at (x, y), wrapping at maxChars-per-line, advancing y
// by lineH per line.
void drawWrapped(M5Canvas& gfx, const char* text, int16_t x, int16_t y, int16_t lineH, uint8_t maxChars) {
    char buf[160];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* word = strtok(buf, " ");
    char line[64] = "";
    int16_t cy = y;

    while (word) {
        size_t lineLen = strlen(line);
        size_t wordLen = strlen(word);
        size_t needed = lineLen + (lineLen ? 1 : 0) + wordLen;

        if (needed > maxChars && lineLen > 0) {
            gfx.setCursor(x, cy);
            gfx.print(line);
            cy += lineH;
            line[0] = '\0';
            lineLen = 0;
        }
        if (lineLen) strcat(line, " ");
        strcat(line, word);
        word = strtok(nullptr, " ");
    }
    if (line[0]) {
        gfx.setCursor(x, cy);
        gfx.print(line);
    }
}
}  // namespace

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
