#pragma once

#include "Screen.h"
#include <cstdint>

// "NETRUNNER" home screen (Fase 56): the entry point BootScreen hands off
// to, replacing the old flat top-level list. Three big navigable tiles -
// WIFI TOOLS on the left, BLUETOOTH TOOLS on the right, TERMINAL across
// the bottom - plus footer hotkeys for SETTINGS and ABOUT. Same tile
// language as the mockup the user supplied; scaled to the real 240x135
// display instead of the 1080x600 mockup canvas.
class HomeScreen : public Screen {
public:
    static HomeScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "HOME"; }
    const char* helpText() const override {
        return "NETRUNNER HOME\n\nArrows: move between tiles\nENTER: open selected\nS: settings   A: about\nDEL: back to boot";
    }

private:
    enum class Tile : uint8_t { Wifi = 0, Bluetooth = 1, Terminal = 2 };

    void drawTile(M5Canvas& gfx, int16_t x, int16_t y, int16_t w, int16_t h,
                  const char* label, uint16_t frameColor, bool selected, uint8_t iconKind);
    void drawWifiIcon(M5Canvas& gfx, int16_t cx, int16_t cy, uint16_t color);
    void drawBtIcon(M5Canvas& gfx, int16_t cx, int16_t cy, uint16_t color);
    void drawTerminalIcon(M5Canvas& gfx, int16_t x, int16_t y, uint16_t color);
    void drawHomeHeader(M5Canvas& gfx);

    Tile _selected = Tile::Wifi;
};
