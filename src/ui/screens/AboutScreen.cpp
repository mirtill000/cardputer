#include "AboutScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../TextWrap.h"

AboutScreen& AboutScreen::instance() {
    static AboutScreen s;
    return s;
}

void AboutScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Back) g_ui.popScreen();
}

namespace {
void kv(M5Canvas& gfx, int16_t y, const char* k, const char* v, uint16_t vColor) {
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, y);
    gfx.print(k);
    gfx.setTextColor(vColor, theme::BG);
    gfx.setCursor(6 + 12 * theme::GLYPH_W, y);
    gfx.print(v);
}
}  // namespace

void AboutScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "ABOUT");

    // Title.
    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, 18);
    gfx.setTextSize(2);
    gfx.print("NETRUNNER");
    gfx.setTextSize(1);
    gfx.setTextColor(theme::CYAN, theme::BG);
    gfx.setCursor(6, 36);
    gfx.print("Advanced Network Toolkit");

    // Facts panel.
    int16_t y = 52;
    kv(gfx, y, "version",  "1.0-ADV",         theme::GREEN); y += 10;
    kv(gfx, y, "board",    "Cardputer ADV",    theme::GREEN); y += 10;
    kv(gfx, y, "mcu",      "ESP32-S3FN8",      theme::GREEN); y += 10;
    kv(gfx, y, "flash",    "8 MB (no PSRAM)",  theme::GREEN); y += 10;
    kv(gfx, y, "radios",   "WiFi + BLE",       theme::CYAN);  y += 10;

    // Short blurb, wrapped.
    gfx.setTextColor(theme::AMBER, theme::BG);
    drawWrapped(gfx,
                "WiFi discovery, port scan, credential + service audit, "
                "war-driving, MITM, PMKID, BLE inventory + GATT posture. "
                "Use ONLY on networks you own or are authorized to test.",
                6, y + 2, 8, 38);

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back");
}
