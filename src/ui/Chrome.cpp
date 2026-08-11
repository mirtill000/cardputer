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
// drawRect calls, no fill). Heights are tuned for a ~26px-tall band,
// max height kMaxBuildingH below.
struct Building {
    int16_t x, w, h;
    bool antenna;  // a thin spike above the roofline, on a few of the tallest
};
constexpr int16_t kMaxBuildingH = 26;
constexpr Building kSkyline[] = {
    {0, 12, 12, false},  {14, 10, 20, false}, {26, 14, 9, false},  {42, 11, 24, true},
    {55, 16, 14, false}, {73, 10, 26, true},  {85, 13, 10, false}, {100, 15, 22, false},
    {117, 11, 16, false}, {130, 14, 26, true}, {146, 10, 12, false}, {158, 16, 20, false},
    {176, 11, 9, false}, {189, 14, 24, true}, {205, 10, 15, false}, {217, 13, 22, false},
    {232, 8, 11, false},
};

// Cyan (short buildings) -> magenta (tall ones), interpolated per-
// building by height. Both endpoints keep blue maxed out (matches
// theme::CYAN and theme::MAGENTA exactly at t=0/t=1 - verified against
// a throwaway reference before use, see git history), only red/green
// trade off - a cheap way to get a skyline that reads as a gradient
// without any real alpha blending.
uint16_t skylineColor(int16_t h) {
    float t = (float)h / (float)kMaxBuildingH;
    if (t > 1.f) t = 1.f;
    uint16_t r = (uint16_t)(31.f * t + 0.5f);
    uint16_t g = (uint16_t)(63.f * (1.f - t) + 0.5f);
    return (uint16_t)((r << 11) | (g << 5) | 0x1F);
}
}  // namespace

void chrome::drawSkyline(M5Canvas& gfx, int16_t baselineY) {
    gfx.drawFastHLine(0, baselineY, gfx.width(), theme::GREY);
    for (const auto& b : kSkyline) {
        uint16_t color = skylineColor(b.h);
        int16_t top = baselineY - b.h;
        gfx.drawRect(b.x, top, b.w, b.h, color);
        if (b.antenna) {
            gfx.drawFastVLine(b.x + b.w / 2, top - 5, 5, color);
        }
    }
}

void chrome::drawPerspectiveGrid(M5Canvas& gfx, int16_t top, int16_t bottom, uint16_t color) {
    int16_t width = gfx.width();
    int16_t centerX = width / 2;
    int16_t height = bottom - top;
    if (height <= 0) return;

    // Horizontal lines: denser near the horizon (top), spread out going
    // toward the viewer (bottom) - a squared falloff is a cheap stand-in
    // for real perspective foreshortening.
    constexpr int kHLines = 5;
    for (int i = 1; i <= kHLines; i++) {
        float t = (float)i / (float)kHLines;
        int16_t y = top + (int16_t)((float)height * t * t);
        gfx.drawFastHLine(0, y, width, color);
    }

    // Converging verticals: fan out from the vanishing point (centerX,
    // top) to evenly spaced points along the bottom edge.
    constexpr int kVLines = 9;
    for (int i = 0; i <= kVLines; i++) {
        int16_t x = (int16_t)(((int32_t)width * i) / kVLines);
        gfx.drawLine(centerX, top, x, bottom, color);
    }
}

void chrome::drawDigitalFog(M5Canvas& gfx, int16_t x, int16_t y, int16_t w, int16_t h) {
    if (w <= 0 || h <= 0) return;

    // Density scales with area rather than being a fixed dot count, so
    // this reads equally "light" whether called over a small panel or
    // the full 240x135 screen - roughly one dot per 55px^2, tuned by
    // eye against the full-screen boot-splash case.
    int32_t area = (int32_t)w * (int32_t)h;
    int dots = (int)(area / 55);
    if (dots > 400) dots = 400;  // hard ceiling regardless of area - stays cheap even if misused on something huge

    // Reseeded on a coarse time bucket (not per-frame) - see the header
    // comment for why a fixed pattern wouldn't read as fog, and why a
    // full per-frame reshuffle would read as static instead of drift.
    // 400ms (slowed down from an initial 150ms, on user feedback that
    // it drifted too fast) keeps the shimmer gentle without reading as
    // a fixed pattern either.
    uint32_t seed = (uint32_t)(millis() / 400) * 2654435761u + 1u;
    for (int i = 0; i < dots; i++) {
        seed = seed * 1103515245u + 12345u;
        int16_t dx = x + (int16_t)(seed % (uint32_t)w);
        seed = seed * 1103515245u + 12345u;
        int16_t dy = y + (int16_t)(seed % (uint32_t)h);
        gfx.drawPixel(dx, dy, theme::GREEN_DIM);
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
