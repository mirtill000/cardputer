#pragma once

#include <cstdint>
#include "../core/Types.h"

// Cyberpunk/Matrix palette, RGB565. Kept as plain constants (not a
// runtime-configurable theme system) because the whole point of this UI
// is a single, consistent look — a theme switcher would be an
// abstraction nobody asked for.
namespace theme {

constexpr uint16_t BG           = 0x0000;  // black
constexpr uint16_t PANEL_BG     = 0x0841;  // near-black, faint blue-green tint for panels
constexpr uint16_t GREEN        = 0x07E0;  // primary phosphor green
constexpr uint16_t GREEN_DIM    = 0x0300;  // rain trail / secondary text
constexpr uint16_t GREEN_BRIGHT = 0xCFF9;  // rain head / highlighted row
constexpr uint16_t CYAN         = 0x07FF;  // info accents
constexpr uint16_t MAGENTA      = 0xF81F;  // warning accents (Cyberpunk 2077-style)
constexpr uint16_t AMBER        = 0xFDA0;  // warning risk level
constexpr uint16_t RED          = 0xF800;  // critical risk / errors
constexpr uint16_t GREY         = 0x4208;  // borders, disabled text

// Classic fixed 6x8 GLCD font (LovyanGFX "font 1") at text size 1. Chosen
// over a proportional/anti-aliased font because (a) it's baked into every
// LovyanGFX/M5GFX build so it costs zero extra flash, and (b) a true
// monospace grid is what makes the "terminal" table layout (host list,
// port list) line up without manual column-width math.
constexpr int GLYPH_W = 6;
constexpr int GLYPH_H = 8;

inline uint16_t riskColor(RiskLevel r) {
    switch (r) {
        case RiskLevel::Critical: return RED;
        case RiskLevel::Warning:  return AMBER;
        default:                  return GREEN;
    }
}

}  // namespace theme
