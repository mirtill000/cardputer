#include "Chrome.h"
#include "Theme.h"
#include "../net/WifiManager.h"
#include <M5Unified.h>
#include <cstdio>
#include <cstring>

namespace {
// Static, deliberately not randomized: this redraws every frame (~30fps),
// so anything non-deterministic here would jitter instead of animate.
// Purely decorative — outline-only rects, cheap to draw (a handful of
// drawRect calls, no fill). Heights are tuned for a ~24px-tall band.
struct Building {
    int16_t x, w, h;
    bool cyan;
};
constexpr Building kSkyline[] = {
    {2, 14, 14, true},  {18, 10, 20, false}, {30, 16, 10, true},  {48, 12, 24, false},
    {62, 18, 16, true}, {82, 10, 22, false}, {94, 14, 12, true},  {110, 20, 26, false},
    {132, 12, 14, true}, {146, 16, 20, false}, {164, 10, 12, true}, {176, 22, 24, false},
    {200, 14, 16, true}, {216, 18, 10, false},
};
}  // namespace

void chrome::drawSkyline(M5Canvas& gfx, int16_t baselineY) {
    gfx.drawFastHLine(0, baselineY, gfx.width(), theme::GREY);
    for (const auto& b : kSkyline) {
        gfx.drawRect(b.x, baselineY - b.h, b.w, b.h, b.cyan ? theme::CYAN : theme::MAGENTA);
    }
}

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
