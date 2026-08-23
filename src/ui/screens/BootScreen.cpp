#include "BootScreen.h"
#include "MainMenuScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../Sound.h"
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
// Back to a short fixed gap: sound::startBootLoop() (see below) is
// non-blocking now - it just spins up a background task and returns
// immediately, unlike the old one-shot playBootJingle() this replaced,
// which blocked the render task for its ~1.5s duration and needed this
// gap widened to not have the prompt pop up mid-note. Nothing here
// blocks anymore, so there's nothing to wait out.
constexpr uint32_t kPromptDelayMs = kTitleDelayMs + 600;

// Splash layout, once the boot log phase hands off to the branded view.
// Redesigned to track the reference NETRUNNER mockup more closely:
// bracketed header, skyline, a synthwave perspective grid floor, version
// line, blinking prompt - no wifi/battery status or uptime/IP row here
// anymore (that's what MAIN MENU's status bar is for, one screen later;
// dropping it here freed the vertical room the grid floor needed).
constexpr int16_t kHeaderY = 4;
constexpr int16_t kTitleY = 18;
constexpr int16_t kSubtitleY = 38;
constexpr int16_t kSkylineBaseline = 80;
constexpr int16_t kGridTop = 80;
constexpr int16_t kGridBottom = 112;
constexpr int16_t kVersionY = 116;
constexpr int16_t kPromptY = 125;
}  // namespace

void BootScreen::onEnter() {
    _enterMs = millis();
    _linesShown = 0;
    _titleShown = false;
    _promptShown = false;
}

void BootScreen::onExit() {
    // Splash music is scoped to this screen only - stop it the moment
    // we leave for MAIN MENU (see sound::stopBootLoop()'s doc comment
    // for the brief unavoidable tail).
    sound::stopBootLoop();
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

    if (!_titleShown && elapsed >= kTitleDelayMs) {
        _titleShown = true;
        // Starts exactly when the boot log hands off to the branded
        // view - "after loading" as literally as this UI has a moment
        // for - and keeps looping for as long as this screen is up;
        // see onExit() for where it stops.
        sound::startBootLoop();
    }
    if (_titleShown && elapsed >= kPromptDelayMs) _promptShown = true;
}

void BootScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);

    if (!_titleShown) {
        // Transient boot-log phase: plain typed lines, no chrome yet —
        // this is meant to read as low-level init, before the branded
        // dashboard view takes over.
        gfx.setTextSize(1);
        gfx.setTextColor(theme::GREEN_DIM, theme::BG);
        for (uint8_t i = 0; i < _linesShown; i++) {
            gfx.setCursor(4, 4 + i * 10);
            gfx.print(kBootLines[i]);
        }
        return;
    }

    // Light digital fog, laid down before everything else on the
    // branded view so title/subtitle/version text (each glyph cell
    // fills its own background when printed) cleanly erases any dots
    // that happen to fall under it - see chrome::drawDigitalFog's
    // comment.
    chrome::drawDigitalFog(gfx, 0, 0, gfx.width(), gfx.height());

    // Decorative bracketed header - deliberately not chrome::drawHeader:
    // this is a splash, not a working screen, so there's no wifi/battery
    // status worth showing yet, and the mockup's header has no divider
    // line under it either.
    {
        const char* text = "-( CARDPUTER ADV )-";
        int16_t w = (int16_t)strlen(text) * theme::GLYPH_W;
        gfx.setTextColor(theme::CYAN, theme::BG);
        gfx.setCursor((gfx.width() - w) / 2, kHeaderY);
        gfx.print(text);
    }

    gfx.setTextSize(2);
    gfx.setTextColor(theme::MAGENTA, theme::BG);
    const char* title = "NETRUNNER";
    int16_t w = (int16_t)strlen(title) * theme::GLYPH_W * 2;
    gfx.setCursor((gfx.width() - w) / 2, kTitleY);
    gfx.print(title);

    gfx.setTextSize(1);
    gfx.setTextColor(theme::CYAN, theme::BG);
    const char* sub = "ADVANCED NETWORK TOOLKIT";
    int16_t w2 = (int16_t)strlen(sub) * theme::GLYPH_W;
    gfx.setCursor((gfx.width() - w2) / 2, kSubtitleY);
    gfx.print(sub);

    chrome::drawSkyline(gfx, kSkylineBaseline);
    chrome::drawPerspectiveGrid(gfx, kGridTop, kGridBottom, theme::MAGENTA);

    gfx.setTextColor(theme::GREY, theme::BG);
    const char* ver = "v1.0.0-ADV";
    int16_t w3 = (int16_t)strlen(ver) * theme::GLYPH_W;
    gfx.setCursor((gfx.width() - w3) / 2, kVersionY);
    gfx.print(ver);

    if (_promptShown) {
        const char* p = "[ PRESS ENTER ]";
        int16_t w4 = (int16_t)strlen(p) * theme::GLYPH_W;
        int16_t x = (gfx.width() - w4) / 2;
        // Always erase the cell first: draw() runs every frame, so a
        // toggling blink has to actively repaint the "off" state too, or
        // the last-drawn text just stays lit forever.
        gfx.fillRect(x, kPromptY, w4, theme::GLYPH_H, theme::BG);
        if ((millis() / 500) % 2 == 0) {
            gfx.setTextColor(theme::MAGENTA, theme::BG);
            gfx.setCursor(x, kPromptY);
            gfx.print(p);
        }
    }
}
