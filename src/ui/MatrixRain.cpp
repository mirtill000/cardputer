#include "MatrixRain.h"
#include "Theme.h"

namespace {
// ASCII-only "hacker" glyph set — the bundled 6x8 font has no katakana,
// so we lean on digits/symbols instead to still read as "matrix-ish".
constexpr char kGlyphs[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ$%#@&*+=<>/\\|:;.";
constexpr size_t kGlyphCount = sizeof(kGlyphs) - 1;
}  // namespace

void MatrixRain::begin(M5Canvas* canvas, int16_t viewX, int16_t viewY, int16_t viewW, int16_t viewH, uint8_t density) {
    _canvas = canvas;
    _viewX = viewX;
    _viewY = viewY;
    _cols = viewW / theme::GLYPH_W;
    _rows = viewH / theme::GLYPH_H;
    if (_cols < 1) _cols = 1;
    if (_rows < 1) _rows = 1;

    _columns.assign(_cols, Column{});
    setDensity(density);

    // Seed the initially-active columns at varied heights so the effect
    // looks "already running" on the very first frame instead of every
    // column starting in lockstep at the top.
    for (auto& c : _columns) {
        if (c.active) resetColumn(c, /*randomizeStartRow=*/true);
    }
}

void MatrixRain::setDensity(uint8_t density) {
    if (_columns.empty()) {
        _density = density;
        return;
    }
    // Not std::min<uint8_t>()/min<uint8_t>(): the Arduino core defines
    // min/max as macros in some configurations, which breaks explicit
    // template-argument call syntax. A plain clamp sidesteps that.
    uint8_t maxCols = (uint8_t)_columns.size();
    _density = (density > maxCols) ? maxCols : density;

    uint8_t activeCount = 0;
    for (auto& c : _columns) activeCount += c.active ? 1 : 0;

    while (activeCount < _density) {
        int16_t idx = random(_cols);
        if (!_columns[idx].active) {
            resetColumn(_columns[idx], /*randomizeStartRow=*/true);
            activeCount++;
        }
    }
    while (activeCount > _density) {
        int16_t idx = random(_cols);
        if (_columns[idx].active) {
            _columns[idx].active = false;
            drawGlyph(idx, _columns[idx].row, ' ', theme::BG);
            activeCount--;
        }
    }
}

void MatrixRain::scatter() {
    for (auto& c : _columns) {
        if (c.active) resetColumn(c, /*randomizeStartRow=*/true);
    }
}

void MatrixRain::resetColumn(Column& c, bool randomizeStartRow) {
    c.active = true;
    c.trailLen = 4 + random(6);       // 4..9 glyphs long
    c.speedTicks = 1 + random(3);     // some columns faster than others
    c.tickCounter = 0;
    c.row = randomizeStartRow ? (int16_t)random(-c.trailLen, _rows) : (int16_t)(-c.trailLen);
}

char MatrixRain::randomGlyph() const {
    return kGlyphs[random(kGlyphCount)];
}

void MatrixRain::drawGlyph(int16_t col, int16_t row, char c, uint16_t color) {
    if (!_canvas || row < 0 || row >= _rows) return;
    _canvas->setTextColor(color, theme::BG);
    _canvas->setCursor(_viewX + col * theme::GLYPH_W, _viewY + row * theme::GLYPH_H);
    _canvas->print(c);
}

void MatrixRain::update() {
    if (!_canvas) return;

    for (int16_t idx = 0; idx < _cols; idx++) {
        Column& c = _columns[idx];
        if (!c.active) continue;

        if (++c.tickCounter < c.speedTicks) continue;
        c.tickCounter = 0;

        c.row++;

        if (c.row - c.trailLen > _rows) {
            // Fully scrolled off the bottom — pick a fresh spawn point
            // (possibly a different column) rather than always
            // respawning in place, so the effect doesn't look tiled.
            c.active = false;
            int16_t newIdx = random(_cols);
            resetColumn(_columns[newIdx], /*randomizeStartRow=*/false);
            continue;
        }

        int16_t eraseRow = c.row - c.trailLen;
        if (eraseRow >= 0) drawGlyph(idx, eraseRow, ' ', theme::BG);

        int16_t dimRow = c.row - 1;
        if (dimRow >= 0) drawGlyph(idx, dimRow, randomGlyph(), theme::GREEN_DIM);

        drawGlyph(idx, c.row, randomGlyph(), theme::GREEN_BRIGHT);
    }
}
