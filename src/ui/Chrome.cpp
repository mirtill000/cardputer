#include "Chrome.h"
#include "Theme.h"
#include "../net/WifiManager.h"
#include <M5Unified.h>
#include <cstdio>
#include <cstring>

void chrome::drawHeader(M5Canvas& gfx, const char* title) {
    gfx.setTextColor(theme::CYAN, theme::BG);
    gfx.setCursor(4, 4);
    gfx.print(">> ");
    gfx.print(title);

    // Right-aligned status: "W <battery%>". getBatteryLevel() returns
    // -1 when the fuel gauge can't report a value (e.g. running off
    // USB with no battery attached) — shown as "--" rather than hidden,
    // so the field's position doesn't shift screen to screen.
    char battBuf[4];
    int32_t batt = M5.Power.getBatteryLevel();
    if (batt >= 0 && batt <= 100) {
        snprintf(battBuf, sizeof(battBuf), "%d", (int)batt);
    } else {
        snprintf(battBuf, sizeof(battBuf), "--");
    }

    int battLen = (int)strlen(battBuf);
    int totalChars = 2 /* "W " */ + battLen + 1 /* "%" */;
    int x = gfx.width() - totalChars * theme::GLYPH_W - 2;
    if (x < 0) x = 0;

    uint16_t wifiColor = g_wifi.isConnected() ? theme::CYAN : theme::GREY;
    gfx.setTextColor(wifiColor, theme::BG);
    gfx.setCursor(x, 4);
    gfx.print("W ");

    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.print(battBuf);
    gfx.print('%');

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, 15, gfx.width() - 8, theme::GREY);
}
