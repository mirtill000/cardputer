#pragma once

#include <M5GFX.h>
#include <vector>

// Background "digital rain" effect: columns of random glyphs scrolling
// down a viewport. Used full-screen on the boot splash, and as a dimmed
// strip behind menu screens.
//
// Perf/memory note: with no PSRAM and a ~135px-tall display we can't
// afford to redraw the whole viewport every tick (that's 240x135x2 bytes
// pushed over SPI, ~20-30 times/sec, just for decoration). Instead each
// tick only touches 2-3 glyph cells per active column: draw a new bright
// head glyph, recolor the previous head to the dim trail color, and
// blank the glyph that just fell off the trail's tail. That keeps the
// per-frame cost roughly O(activeColumns) instead of O(viewport area).
class MatrixRain {
public:
    // canvas must already have had its font configured to the classic
    // 6x8 font (theme::GLYPH_W/H) — MatrixRain does not touch font state
    // itself so it can share a canvas with other UI drawing without
    // fighting over font selection.
    void begin(M5Canvas* canvas, int16_t viewX, int16_t viewY, int16_t viewW, int16_t viewH, uint8_t density);

    void setDensity(uint8_t density);

    // Re-randomizes active columns' vertical positions so the effect
    // looks already-running the instant a screen becomes visible again,
    // instead of every column restarting from the top.
    void scatter();

    // Advances the animation by one tick and draws the changed glyphs.
    // Call at ~12-20 Hz from the UI task; calling faster just wastes
    // cycles since column speed is independent of call rate (see
    // Column::speedTicks below).
    void update();

private:
    struct Column {
        bool active = false;
        int16_t row = 0;         // current head row (can be negative: still entering from top)
        uint8_t trailLen = 6;
        uint8_t speedTicks = 1;  // ticks between advances; higher = slower column
        uint8_t tickCounter = 0;
    };

    void resetColumn(Column& c, bool randomizeStartRow);
    void drawGlyph(int16_t col, int16_t row, char c, uint16_t color);
    char randomGlyph() const;

    M5Canvas* _canvas = nullptr;
    int16_t _viewX = 0, _viewY = 0;
    int16_t _cols = 0, _rows = 0;
    uint8_t _density = 0;
    std::vector<Column> _columns;
};
