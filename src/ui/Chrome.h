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

// Decorative "neon skyline" silhouette — a row of outline rects with
// their bottom edge on baselineY, color-graded cyan (short buildings) to
// magenta (tall ones) with a couple of thin antenna spikes on the
// tallest towers, plus a horizontal rule at baselineY. Only BootScreen
// uses this now (MainMenuScreen's own copy was removed at the user's
// request — see git history) but it's kept here rather than folded
// into BootScreen.cpp since it's still "the shared decorative chrome"
// conceptually, alongside drawPerspectiveGrid below.
void drawSkyline(M5Canvas& gfx, int16_t baselineY);

// Synthwave-style perspective grid floor: horizontal lines spaced with
// a squared falloff (dense near the horizon at `top`, sparse near
// `bottom`) and a fan of lines converging on a single vanishing point
// at the horizon's center — a cheap approximation of a 3D perspective
// grid using only drawLine()/drawFastHLine(), no real projection math.
void drawPerspectiveGrid(M5Canvas& gfx, int16_t top, int16_t bottom, uint16_t color);

// Sparse, dim, drifting single-pixel dots over the given rect — a
// light "digital fog"/static texture. Unlike drawSkyline/
// drawPerspectiveGrid above, this one is deliberately reseeded over
// time (not a fixed per-call pattern): a completely static dot field
// wouldn't read as fog at all, just as noise baked into the
// background. Reseeded roughly every 400ms rather than every frame, so
// it drifts/shimmers gently instead of flickering like TV static -
// "leggera" (light), meant to be glanced past, not stared at. Call
// this before drawing anything else that must stay fully legible over
// it (title text, etc.) - every glyph cell this app prints fills its
// own background as part of printing, so text drawn afterward cleanly
// erases any dots underneath it.
void drawDigitalFog(M5Canvas& gfx, int16_t x, int16_t y, int16_t w, int16_t h);

}  // namespace chrome
