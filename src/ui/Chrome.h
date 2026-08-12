#pragma once

#include <M5GFX.h>
#include <WiFi.h>  // wifi_auth_mode_t for the security-label helpers

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

// --- Shared WiFi-list widgets (used by WIFI SCAN and WAR DRIVING so both
// render RSSI/security identically). ---

// Four-bar signal meter, ~18px wide, bars growing left-to-right, bottom
// edge on baselineY. Filled bars up to the RSSI's strength level; the
// rest drawn as empty grey outlines. Color: green (strong) / amber
// (medium) / red (weak).
void drawSignalBars(M5Canvas& gfx, int16_t x, int16_t baselineY, int32_t rssi);

// Small (~7x7) stylised WiFi glyph (dot + two arcs) at top-left (x, y).
void drawWifiIcon(M5Canvas& gfx, int16_t x, int16_t y, uint16_t color);

// Short security label ("OPEN"/"WEP"/"WPA"/"WPA2"/"WPA2-ENT"/"WPA3") and
// a matching color (weaker schemes lean red/amber, WPA2+/enterprise
// green/cyan) for a scan result's encryption mode.
const char* securityLabel(wifi_auth_mode_t enc);
uint16_t securityColor(wifi_auth_mode_t enc);

// --- Shared status/feedback widgets (Fase 24 UX pass) ---

// Horizontal progress bar: grey outline with a cyan fill proportional to
// pct (0-100), and the percentage printed to its right.
void drawProgressBar(M5Canvas& gfx, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t pct);

// A single spinning-character busy indicator at (x, y), animated off
// nowMs. Use for indeterminate "working..." states.
void drawSpinner(M5Canvas& gfx, int16_t x, int16_t y, uint32_t nowMs, uint16_t color);

// Centred, consistent empty/first-run state: a dim title line and an
// amber "what to do next" hint, vertically roughly centred in the body.
void drawEmptyState(M5Canvas& gfx, const char* title, const char* hint);

// Red "danger gate" header used by the authorization/disclaimer screens —
// same layout as drawHeader but red, and no battery/breadcrumb (these are
// modal gates, not part of normal navigation). Centralized so the three
// consent screens share one definition.
void drawAlertHeader(M5Canvas& gfx, const char* title);

}  // namespace chrome
