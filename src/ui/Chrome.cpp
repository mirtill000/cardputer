#include "Chrome.h"
#include "Theme.h"
#include "ActivityStatus.h"
#include "UiManager.h"
#include "TextWrap.h"
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

void chrome::drawSignalBars(M5Canvas& gfx, int16_t x, int16_t baselineY, int32_t rssi) {
    int level = (rssi >= -60) ? 4 : (rssi >= -68) ? 3 : (rssi >= -75) ? 2 : 1;
    uint16_t col = (level >= 3) ? theme::GREEN : (level == 2) ? theme::AMBER : theme::RED;
    for (int i = 0; i < 4; i++) {
        int16_t h = (int16_t)(2 + i * 2);  // 2,4,6,8
        int16_t bx = (int16_t)(x + i * 4);
        int16_t by = (int16_t)(baselineY - h);
        if (i < level)
            gfx.fillRect(bx, by, 3, h, col);
        else
            gfx.drawRect(bx, by, 3, h, theme::GREY);
    }
}

void chrome::drawWifiIcon(M5Canvas& gfx, int16_t x, int16_t y, uint16_t color) {
    gfx.fillCircle(x + 3, y + 6, 1, color);          // base dot
    gfx.drawLine(x + 1, y + 4, x + 3, y + 2, color);  // inner arc
    gfx.drawLine(x + 3, y + 2, x + 5, y + 4, color);
    gfx.drawLine(x, y + 3, x + 3, y, color);          // outer arc
    gfx.drawLine(x + 3, y, x + 6, y + 3, color);
}

const char* chrome::securityLabel(wifi_auth_mode_t enc) {
    switch (enc) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA3";
        default: return "WPA2";
    }
}

uint16_t chrome::securityColor(wifi_auth_mode_t enc) {
    switch (enc) {
        case WIFI_AUTH_OPEN: return theme::AMBER;
        case WIFI_AUTH_WEP: return theme::RED;
        case WIFI_AUTH_WPA_PSK: return theme::AMBER;
        case WIFI_AUTH_WPA2_ENTERPRISE: return theme::CYAN;
        default: return theme::GREEN;
    }
}

void chrome::drawProgressBar(M5Canvas& gfx, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t pct) {
    if (pct > 100) pct = 100;
    gfx.drawRect(x, y, w, h, theme::GREY);
    int16_t fillW = (int16_t)((int32_t)(w - 2) * pct / 100);
    if (fillW > 0) gfx.fillRect(x + 1, y + 1, fillW, h - 2, theme::CYAN);
    char buf[6];
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)pct);
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(x + w + 3, y + (h - 8) / 2);
    gfx.print(buf);
}

void chrome::drawSpinner(M5Canvas& gfx, int16_t x, int16_t y, uint32_t nowMs, uint16_t color) {
    static const char frames[] = {'|', '/', '-', '\\'};
    char c = frames[(nowMs / 120) % 4];
    gfx.setTextColor(color, theme::BG);
    gfx.setCursor(x, y);
    gfx.print(c);
}

void chrome::drawAlertHeader(M5Canvas& gfx, const char* title) {
    gfx.setTextColor(theme::RED, theme::BG);
    gfx.setCursor(4, 4);
    gfx.print(">> ");
    gfx.print(title);
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, 15, gfx.width() - 8, theme::GREY);
}

namespace {
// Linear interpolate between two RGB565 colors (t in 0..255). Used only
// by the accent rule; kept local since nothing else needs it.
uint16_t lerp565(uint16_t a, uint16_t b, uint8_t t) {
    int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    int r = ar + (br - ar) * t / 255;
    int g = ag + (bg - ag) * t / 255;
    int bl = ab + (bb - ab) * t / 255;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}
}  // namespace

void chrome::drawAccentRule(M5Canvas& gfx, int16_t y) {
    // A single thin cyan->magenta rule spanning the usable width, drawn as
    // a handful of colored segments (no per-pixel loop). Gives every
    // screen a hint of the boot screen's neon identity along the bottom.
    const int16_t x0 = 4;
    const int16_t x1 = gfx.width() - 4;
    const int16_t w = x1 - x0;
    if (w <= 0) return;
    constexpr int kSeg = 8;
    for (int i = 0; i < kSeg; i++) {
        int16_t sx = x0 + (int16_t)(w * i / kSeg);
        int16_t ex = x0 + (int16_t)(w * (i + 1) / kSeg);
        uint8_t t = (uint8_t)(255 * i / (kSeg - 1));
        gfx.drawFastHLine(sx, y, ex - sx, lerp565(theme::CYAN, theme::MAGENTA, t));
    }
}

void chrome::drawFooter(M5Canvas& gfx, const char* hints) {
    // Standard footer: the shared accent rule, then the key-hint line at
    // one fixed baseline (height()-9, the offset ~80% of screens already
    // used) in the standard grey. Truncated, never wrapped.
    drawAccentRule(gfx, gfx.height() - 13);
    if (!hints || !hints[0]) return;
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    // Truncate to what fits so a long hint can't spill past the right edge.
    int maxChars = (gfx.width() - 8) / theme::GLYPH_W;
    if ((int)strlen(hints) <= maxChars) {
        gfx.print(hints);
    } else {
        char buf[64];
        int n = maxChars < (int)sizeof(buf) - 1 ? maxChars : (int)sizeof(buf) - 1;
        memcpy(buf, hints, n);
        buf[n] = '\0';
        gfx.print(buf);
    }
}

void chrome::drawScrollMarkers(M5Canvas& gfx, int16_t top, int16_t bottom, bool moreAbove, bool moreBelow) {
    // Right-margin corner markers, same position/color MainMenuScreen's
    // own (now-shared) convention already established. setTextColor's
    // bg arg means each glyph cell fills its own background as it
    // prints, so this cleanly overlays whatever the list already drew
    // underneath without needing a separate clear.
    if (moreAbove) {
        gfx.setTextColor(theme::CYAN, theme::BG);
        gfx.setCursor(gfx.width() - 10, top);
        gfx.print("^");
    }
    if (moreBelow) {
        gfx.setTextColor(theme::CYAN, theme::BG);
        gfx.setCursor(gfx.width() - 10, bottom - 8);
        gfx.print("v");
    }
}

void chrome::drawDetailOverlay(M5Canvas& gfx, const char* title, const String& text) {
    gfx.fillRect(0, 0, gfx.width(), gfx.height(), theme::BG);
    gfx.drawRect(6, 6, gfx.width() - 12, gfx.height() - 12, theme::CYAN);

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(12, 12);
    gfx.print(">> ");
    gfx.print(title);

    gfx.setTextColor(theme::GREEN, theme::BG);
    drawWrapped(gfx, text.c_str(), 12, 26, 10, 37);

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(12, gfx.height() - 16);
    gfx.print("any key: close");
}

void chrome::drawEmptyState(M5Canvas& gfx, const char* title, const char* hint) {
    gfx.setTextColor(theme::GREY, theme::BG);
    int16_t tx = (gfx.width() - (int16_t)strlen(title) * theme::GLYPH_W) / 2;
    if (tx < 4) tx = 4;
    gfx.setCursor(tx, 52);
    gfx.print(title);
    if (hint && hint[0]) {
        gfx.setTextColor(theme::AMBER, theme::BG);
        int16_t hx = (gfx.width() - (int16_t)strlen(hint) * theme::GLYPH_W) / 2;
        if (hx < 4) hx = 4;
        gfx.setCursor(hx, 66);
        gfx.print(hint);
    }
}

void chrome::drawHeader(M5Canvas& gfx, const char* title) {
    gfx.setCursor(4, 4);
    gfx.setTextColor(theme::CYAN, theme::BG);
    gfx.print(">> ");
    // Breadcrumb prefix: the full chain of ancestor titles on the nav
    // stack, dim before this title, so the user can see the ENTIRE path
    // that got them here after deep navigation (e.g. "MENU/NET/DISC/"
    // before "BCN", not just the immediate parent). Purely additive —
    // screens with no title() anywhere in the chain render exactly as
    // before (empty breadcrumb at the root, same as always).
    String parent = g_ui.breadcrumbPath();
    if (parent.length()) {
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.print(parent);
        gfx.print("/");
        gfx.setTextColor(theme::CYAN, theme::BG);
    }
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

    // Battery color by level: red < 15%, amber < 30%, green otherwise
    // (grey when the gauge can't report a value).
    uint16_t battColor = (batt < 0)    ? theme::GREY
                         : (batt < 15) ? theme::RED
                         : (batt < 30) ? theme::AMBER
                                       : theme::GREEN;
    gfx.setTextColor(battColor, theme::BG);
    gfx.print(battBuf);
    gfx.print('%');

    // Background-activity indicator, right-aligned just left of the
    // battery block (draws nothing when nothing is running) — see
    // ActivityStatus.h for why this lives on every screen's header. Also
    // decides the separator line's color below: normally grey, escalating
    // when enough is running at once that the compact tag alone is easy
    // to miss (Fase 37) — see activity::draw's own doc comment.
    uint16_t separatorColor = activity::draw(gfx, (int16_t)(x - 4), 4);
    // Visual identity (Fase harmonization): in the common case — no
    // elevated background activity, so activity::draw asked for the plain
    // grey line — render the separator as the shared cyan->magenta accent
    // instead, so every screen carries a hint of the boot screen's neon
    // identity from this one shared point. An amber/red separator is a
    // real activity/conflict signal and still wins: only the grey default
    // is replaced.
    if (separatorColor == theme::GREY) {
        chrome::drawAccentRule(gfx, 15);
    } else {
        gfx.drawFastHLine(4, 15, gfx.width() - 8, separatorColor);
    }
}
