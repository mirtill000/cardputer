#pragma once

#include <M5GFX.h>
#include <cstdint>

// Minimal greedy word-wrap for the fixed-width 6px font: prints `text`
// starting at (x, y), wrapping at maxChars-per-line, advancing y by
// lineH per line. Shared by any screen with a paragraph of prose to show
// (placeholders, the credential-audit disclaimer, ...).
void drawWrapped(M5Canvas& gfx, const char* text, int16_t x, int16_t y, int16_t lineH, uint8_t maxChars);
