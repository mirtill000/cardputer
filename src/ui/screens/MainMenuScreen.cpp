#include "MainMenuScreen.h"
#include "OffensiveDisclaimerScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Config.h"
#include "../../net/WifiManager.h"
#include "../../net/TimeSync.h"
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
        case UiKey::Enter: {
            const MenuItem& item = _items[_selected];
            if (!item.target) break;
            if (item.offensive && !g_config.offensiveEnabled) {
                OffensiveDisclaimerScreen::instance().setPendingTargetScreen(item.target);
                g_ui.pushScreen(&OffensiveDisclaimerScreen::instance());
            } else {
                g_ui.pushScreen(item.target);
            }
            break;
        }
        default:
            break;
    }
}

void MainMenuScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "NETRUNNER");

    // kRowH sized so a full window of rows never runs into the status
    // bar below: kTop + 7*kRowH = 22 + 98 = 120, status bar sits at
    // height-9 = 126, leaving a 6px gap. The menu grew past 7 entries
    // (Fase 18 added LAN TOPOLOGY / UPNP DISCOVERY / ROGUE DHCP - see
    // main.cpp), so it now scrolls: only kVisibleRows are drawn at a
    // time, following _selected, with ^/v markers when more exist off-
    // screen.
    constexpr int16_t kRowH = 14;
    constexpr int16_t kTop = 22;
    constexpr uint8_t kVisibleRows = 7;

    uint8_t first = 0;
    if (_selected >= kVisibleRows) first = (uint8_t)(_selected - kVisibleRows + 1);

    for (uint8_t row = 0; row < kVisibleRows; row++) {
        uint8_t i = (uint8_t)(first + row);
        if (i >= _count) break;
        int16_t y = kTop + row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        gfx.drawRect(4, y, gfx.width() - 8, kRowH - 2, sel ? theme::CYAN : theme::GREY);
        gfx.fillRect(5, y + 1, gfx.width() - 10, kRowH - 4, rowBg);

        gfx.setTextColor(sel ? theme::MAGENTA : theme::GREY, rowBg);
        gfx.setCursor(10, y + 2);
        gfx.print(sel ? "> " : "  ");
        gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
        gfx.print(_items[i].label);

        // Scroll markers on the row's right edge (share the row's bg so
        // they read cleanly whether or not the row is selected).
        if (row == 0 && first > 0) {
            gfx.setTextColor(theme::CYAN, rowBg);
            gfx.setCursor(gfx.width() - 14, y + 2);
            gfx.print("^");
        }
        if (row == kVisibleRows - 1 && (first + kVisibleRows) < _count) {
            gfx.setTextColor(theme::CYAN, rowBg);
            gfx.setCursor(gfx.width() - 14, y + 2);
            gfx.print("v");
        }
    }

    // Bottom status bar: wall-clock time (UTC) once NTP has synced (see
    // net/TimeSync.h), uptime otherwise / READY. / current IP.
    // Replaces the old key-hint footer on this one screen — Up/Down/
    // Enter is already the established convention by the time a user
    // reaches the menu.
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
