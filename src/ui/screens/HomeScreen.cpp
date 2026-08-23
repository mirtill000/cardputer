#include "HomeScreen.h"
#include "MainMenuScreen.h"
#include "BluetoothToolsMenuScreen.h"
#include "AboutScreen.h"
#include "SettingsScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include <M5Unified.h>

HomeScreen& HomeScreen::instance() {
    static HomeScreen s;
    return s;
}

void HomeScreen::onEnter() {
    // Keep last selection so re-entering feels stable.
}

void HomeScreen::onKey(UiKey key, char ch) {
    switch (key) {
        case UiKey::Left:
            _selected = Tile::Wifi;
            break;
        case UiKey::Right:
            _selected = Tile::Bluetooth;
            break;
        case UiKey::Enter:
            if (_selected == Tile::Wifi) g_ui.pushScreen(&MainMenuScreen::instance());
            else                          g_ui.pushScreen(&BluetoothToolsMenuScreen::instance());
            break;
        case UiKey::Char:
            if (ch == 's' || ch == 'S') g_ui.pushScreen(&SettingsScreen::instance());
            else if (ch == 'a' || ch == 'A') g_ui.pushScreen(&AboutScreen::instance());
            break;
        case UiKey::Back:
            // No-op - HOME is the top of the navigable stack. There's
            // nothing to go back to (BootScreen has already replaced
            // itself out).
            break;
        default:
            break;
    }
}

void HomeScreen::drawHomeHeader(M5Canvas& gfx) {
    // Slim header row matching chrome::drawHeader's look (magenta rule +
    // right-aligned battery), but without the breadcrumb — HOME is the
    // top. Battery from M5.Power.getBatteryLevel(), same rule the shared
    // chrome uses.
    constexpr int16_t kHeaderH = 10;
    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(4, 1);
    gfx.print("CARDPUTER ADV");

    // Right side: WiFi glyph + battery %.
    int32_t batt = M5.Power.getBatteryLevel();
    char bb[8];
    snprintf(bb, sizeof(bb), "%3d%%", (int)((batt < 0) ? 0 : batt));
    uint16_t battColor = (batt < 0)     ? theme::GREY
                         : (batt < 15) ? theme::RED
                         : (batt < 30) ? theme::AMBER
                                       : theme::GREEN;
    int16_t bx = gfx.width() - (int16_t)strlen(bb) * theme::GLYPH_W - 4;
    gfx.setTextColor(battColor, theme::BG);
    gfx.setCursor(bx, 1);
    gfx.print(bb);
    // Simple wifi glyph left of the % - two dots stacked as a compact
    // 5x5 icon rather than the fuller chrome::drawWifiIcon (that one is
    // sized for the standard 12-px header).
    chrome::drawWifiIcon(gfx, bx - 10, 1, theme::CYAN);

    gfx.drawFastHLine(0, kHeaderH, gfx.width(), theme::MAGENTA);
}

void HomeScreen::drawWifiIcon(M5Canvas& gfx, int16_t cx, int16_t cy, uint16_t color) {
    // Three concentric arcs (open at the bottom) + dot below. Drawn with
    // circle segments approximated by fillCircle + BG cut for the arc
    // effect, cheap on M5GFX.
    for (int r = 8; r >= 4; r -= 2) {
        gfx.drawCircle(cx, cy, r, color);
    }
    gfx.fillCircle(cx, cy, 1, color);
    // Cut the lower halves to make them read as arcs.
    gfx.fillRect(cx - 10, cy + 1, 21, 10, theme::BG);
    // Redraw the dot which the rect just erased.
    gfx.fillCircle(cx, cy - 3, 1, color);
}

void HomeScreen::drawBtIcon(M5Canvas& gfx, int16_t cx, int16_t cy, uint16_t color) {
    // Bluetooth rune - a stylized "B": vertical spine, two diagonals
    // forming an X above and below, meeting at the midline. Drawn from
    // 5 line segments centered at (cx, cy) with total height 14.
    int16_t x0 = cx;
    int16_t top = cy - 7;
    int16_t bot = cy + 7;
    int16_t mid = cy;
    int16_t right = cx + 5;
    // vertical
    gfx.drawLine(x0, top, x0, bot, color);
    // upper: top -> right-mid
    gfx.drawLine(x0, top, right, mid - 3, color);
    // upper: right-mid -> mid (comes back to the spine's midline)
    gfx.drawLine(right, mid - 3, x0, mid, color);
    // lower: mid -> right-mid
    gfx.drawLine(x0, mid, right, mid + 3, color);
    // lower: right-mid -> bot
    gfx.drawLine(right, mid + 3, x0, bot, color);
    // corners cross-slashes (finish the rune)
    gfx.drawLine(x0 - 4, mid - 3, right, mid - 3, color);  // faint top bar (subtle)
    gfx.drawLine(x0 - 4, mid + 3, right, mid + 3, color);  // faint bot bar
}

void HomeScreen::drawTile(M5Canvas& gfx, int16_t x, int16_t y, int16_t w, int16_t h,
                          const char* label, uint16_t frameColor, bool selected, uint8_t iconKind) {
    // Rounded-look frame: double outline when selected, single otherwise.
    // Fill stays BG - the tile "looks like a viewfinder", matching the
    // mockup's neon-outline aesthetic.
    uint16_t border = frameColor;
    gfx.drawRect(x, y, w, h, border);
    if (selected) {
        gfx.drawRect(x + 1, y + 1, w - 2, h - 2, border);  // thicker border when selected
    }
    // Corner marks so the tile reads as a "frame", not just a box.
    gfx.drawFastHLine(x + 2, y, 3, border);
    gfx.drawFastHLine(x + w - 5, y, 3, border);
    gfx.drawFastHLine(x + 2, y + h - 1, 3, border);
    gfx.drawFastHLine(x + w - 5, y + h - 1, 3, border);

    // Icon (centered upper half).
    int16_t iconY = y + h / 3;
    int16_t iconX = x + w / 2;
    switch (iconKind) {
        case 0: drawWifiIcon(gfx, iconX, iconY, frameColor); break;
        case 1: drawBtIcon(gfx, iconX, iconY, frameColor); break;
        default: break;
    }

    // Vertical stack: icon at top, label near bottom.
    int16_t textLen = (int16_t)strlen(label) * theme::GLYPH_W;
    gfx.setTextColor(selected ? theme::CYAN : frameColor, theme::BG);
    int16_t labelX = x + (w - textLen) / 2 - 6;  // leave room for chevron on right
    int16_t labelY = y + h - 12;
    gfx.setCursor(labelX, labelY);
    gfx.print(label);
    // Right-side chevron ">".
    gfx.setCursor(x + w - 8, y + h - 12);
    gfx.print(">");
}

void HomeScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    drawHomeHeader(gfx);

    // Title: NETRUNNER in magenta (textsize 2 = 12x16 per glyph), centered
    // horizontally, with small decorative marks either side.
    const char* kTitle = "NETRUNNER";
    int16_t titleW = 9 * 12;   // 9 glyphs * 12px
    int16_t titleX = (gfx.width() - titleW) / 2;
    int16_t titleY = 13;
    gfx.setTextSize(2);
    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(titleX, titleY);
    gfx.print(kTitle);
    gfx.setTextSize(1);
    // Little accent triangles + dot either side of the title.
    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(titleX - 12, titleY + 4);
    gfx.print(">");
    gfx.setCursor(titleX + titleW + 6, titleY + 4);
    gfx.print("<");

    // Subtitle + version (single small row).
    gfx.setTextColor(theme::CYAN, theme::BG);
    const char* kSub = "ADVANCED NETWORK TOOLKIT v1.0";
    int16_t subW = (int16_t)strlen(kSub) * theme::GLYPH_W;
    gfx.setCursor((gfx.width() - subW) / 2, titleY + 17);
    gfx.print(kSub);

    // --- Tiles ---
    // Layout: two side-by-side tiles filling the middle band. Fase 57
    // removed the TERMINAL tile (the user found it made the screen too
    // busy), so the WIFI/BT tiles get the freed vertical space and read
    // as the two primary actions. Leave space at top for header+title
    // (~48px) and at bottom for the footer (~11px).
    int16_t bandTop = 52;
    int16_t bandH = 65;
    int16_t tileGap = 4;
    int16_t tileW = (gfx.width() - 12 - tileGap) / 2;  // 4px side margins + gap

    drawTile(gfx, 4, bandTop, tileW, bandH, "WIFI", theme::CYAN,
             _selected == Tile::Wifi, /*iconKind=*/0);
    drawTile(gfx, 4 + tileW + tileGap, bandTop, tileW, bandH, "BT", theme::MAGENTA,
             _selected == Tile::Bluetooth, /*iconKind=*/1);

    // Footer: STATUS on the left, [SETTINGS] [ABOUT] on the right.
    int16_t footerY = gfx.height() - 9;
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, footerY);
    gfx.print("STATUS: ");
    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.print("READY");
    // Right side: [S]ETTINGS [A]BOUT - the "[" is drawn faint so the
    // hotkey letter reads as the important part.
    const char* kRight = "S:set A:about";
    int16_t rightX = gfx.width() - (int16_t)strlen(kRight) * theme::GLYPH_W - 4;
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(rightX, footerY);
    gfx.print(kRight);
}
