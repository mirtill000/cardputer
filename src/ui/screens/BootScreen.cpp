#include "BootScreen.h"
#include "MainMenuScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include <cstring>

namespace {
constexpr const char* kBootLines[] = {
    "[ OK ] esp32-s3 core init",
    "[ OK ] wifi driver loaded",
    "[ OK ] littlefs mounted",
    "[ OK ] oui vendor db loaded",
    "[ OK ] nvs config loaded",
    "[ OK ] ui render task up",
};
constexpr uint8_t kLineCount = sizeof(kBootLines) / sizeof(kBootLines[0]);
constexpr uint32_t kLineIntervalMs = 220;
constexpr uint32_t kTitleDelayMs = (uint32_t)kLineCount * kLineIntervalMs + 300;
constexpr uint32_t kPromptDelayMs = kTitleDelayMs + 600;
}  // namespace

void BootScreen::onEnter() {
    _enterMs = millis();
    _linesShown = 0;
    _titleShown = false;
    _promptShown = false;
}

void BootScreen::onKey(UiKey key, char /*ch*/) {
    if (!_promptShown) return;  // ignore input until the boot sequence has finished
    if (key == UiKey::Enter || key == UiKey::Back) {
        g_ui.replaceScreen(&MainMenuScreen::instance());
    }
}

void BootScreen::update(uint32_t nowMs) {
    uint32_t elapsed = nowMs - _enterMs;

    uint32_t wantLines = elapsed / kLineIntervalMs;
    if (wantLines > kLineCount) wantLines = kLineCount;
    _linesShown = (uint8_t)wantLines;

    if (!_titleShown && elapsed >= kTitleDelayMs) _titleShown = true;
    if (_titleShown && elapsed >= kPromptDelayMs) _promptShown = true;
}

void BootScreen::draw(M5Canvas& gfx) {
    gfx.setTextSize(1);
    gfx.setTextColor(theme::GREEN_DIM, theme::BG);
    for (uint8_t i = 0; i < _linesShown; i++) {
        gfx.setCursor(4, 4 + i * 10);
        gfx.print(kBootLines[i]);
    }

    if (_titleShown) {
        gfx.setTextSize(2);
        gfx.setTextColor(theme::GREEN_BRIGHT, theme::BG);
        const char* title = "CARDPUTER";
        int16_t w = (int16_t)strlen(title) * theme::GLYPH_W * 2;
        gfx.setCursor((gfx.width() - w) / 2, 58);
        gfx.print(title);

        gfx.setTextSize(1);
        gfx.setTextColor(theme::CYAN, theme::BG);
        const char* sub = "NETWORK AUDIT TOOL";
        int16_t w2 = (int16_t)strlen(sub) * theme::GLYPH_W;
        gfx.setCursor((gfx.width() - w2) / 2, 80);
        gfx.print(sub);
    }

    if (_promptShown) {
        const char* p = "[ PRESS ENTER ]";
        int16_t w = (int16_t)strlen(p) * theme::GLYPH_W;
        int16_t x = (gfx.width() - w) / 2;
        int16_t y = 112;
        // Always erase the cell first: draw() runs every frame, so a
        // toggling blink has to actively repaint the "off" state too, or
        // the last-drawn text just stays lit forever.
        gfx.fillRect(x, y, w, theme::GLYPH_H, theme::BG);
        if ((millis() / 500) % 2 == 0) {
            gfx.setTextColor(theme::MAGENTA, theme::BG);
            gfx.setCursor(x, y);
            gfx.print(p);
        }
    }
}
