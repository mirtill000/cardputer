#include "BootScreen.h"
#include "MainMenuScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../net/WifiManager.h"
#include "../../net/TimeSync.h"
#include <cstdio>
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

// Splash layout, once the boot log phase hands off to the branded view —
// mirrors MainMenuScreen's skyline band and bottom status bar so the two
// screens read as one dashboard rather than two different UIs stitched
// together.
constexpr int16_t kSkylineBaseline = 86;
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

    chrome::drawHeader(gfx, "CARDPUTER ADV");

    gfx.setTextSize(2);
    gfx.setTextColor(theme::MAGENTA, theme::BG);
    const char* title = "NETRUNNER";
    int16_t w = (int16_t)strlen(title) * theme::GLYPH_W * 2;
    gfx.setCursor((gfx.width() - w) / 2, 20);
    gfx.print(title);

    gfx.setTextSize(1);
    gfx.setTextColor(theme::CYAN, theme::BG);
    const char* sub = "ADVANCED NETWORK TOOLKIT";
    int16_t w2 = (int16_t)strlen(sub) * theme::GLYPH_W;
    gfx.setCursor((gfx.width() - w2) / 2, 40);
    gfx.print(sub);

    gfx.setTextColor(theme::GREY, theme::BG);
    const char* ver = "v1.0.0-ADV";
    int16_t w3 = (int16_t)strlen(ver) * theme::GLYPH_W;
    gfx.setCursor((gfx.width() - w3) / 2, 50);
    gfx.print(ver);

    chrome::drawSkyline(gfx, kSkylineBaseline);

    if (_promptShown) {
        const char* p = "[ PRESS ENTER ]";
        int16_t w4 = (int16_t)strlen(p) * theme::GLYPH_W;
        int16_t x = (gfx.width() - w4) / 2;
        int16_t y = 98;
        // Always erase the cell first: draw() runs every frame, so a
        // toggling blink has to actively repaint the "off" state too, or
        // the last-drawn text just stays lit forever.
        gfx.fillRect(x, y, w4, theme::GLYPH_H, theme::BG);
        if ((millis() / 500) % 2 == 0) {
            gfx.setTextColor(theme::MAGENTA, theme::BG);
            gfx.setCursor(x, y);
            gfx.print(p);
        }
    }

    // Bottom status bar — same layout as MainMenuScreen's, so the splash
    // reads as the first frame of one continuous dashboard rather than a
    // separate screen with its own conventions. Wall-clock (UTC) once
    // NTP has synced, uptime otherwise - see net/TimeSync.h.
    String leftStr = TimeSync::nowTimeString();
    if (leftStr.isEmpty()) {
        uint32_t upSec = millis() / 1000;
        char upBuf[16];  // "HH:MM:SS" is 9 bytes, but hours isn't clamped - room for a much longer uptime
        snprintf(upBuf, sizeof(upBuf), "%02u:%02u:%02u", (unsigned)(upSec / 3600), (unsigned)((upSec / 60) % 60),
                 (unsigned)(upSec % 60));
        leftStr = upBuf;
    }

    int16_t statusY = gfx.height() - 9;
    gfx.setTextColor(theme::CYAN, theme::BG);
    gfx.setCursor(4, statusY);
    gfx.print(leftStr);

    const char* boot = "SYSTEM READY";
    int16_t bootX = (gfx.width() - (int16_t)strlen(boot) * theme::GLYPH_W) / 2;
    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.setCursor(bootX, statusY);
    gfx.print(boot);

    String ipStr = g_wifi.isConnected() ? g_wifi.localIP().toString() : String("no ip");
    int16_t ipX = gfx.width() - (int16_t)ipStr.length() * theme::GLYPH_W - 4;
    gfx.setTextColor(theme::CYAN, theme::BG);
    gfx.setCursor(ipX, statusY);
    gfx.print(ipStr);
}
