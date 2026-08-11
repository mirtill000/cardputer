#pragma once

#include <M5GFX.h>

// Shared screen chrome: every screen used to hand-roll its own ">> TITLE"
// header + separator line. Centralizing it here (a) keeps that pixel-
// exact layout consistent across ~10 screens instead of copy-pasted,
// and (b) is what adds a live WiFi/battery status indicator to every
// screen at once, matching a status-bar mockup the user asked this UI
// to move towards — without spending one of the 135 vertical pixels on
// a whole separate row for it, since it shares the existing title line.
namespace chrome {

void drawHeader(M5Canvas& gfx, const char* title);

}  // namespace chrome
