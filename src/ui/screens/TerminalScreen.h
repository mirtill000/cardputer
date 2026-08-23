#pragma once

#include "Screen.h"

// "TERMINAL" - the third HomeScreen tile (Fase 56). Not a shell (this
// isn't Linux, there's nothing on the other end of a `>` prompt to
// evaluate arbitrary commands), but a compact device-status readout in
// a monospace terminal look: uptime, RAM/heap, active WiFi + IP + SSID,
// BLE state, host / port / thread counts from the running managers,
// PSRAM (there isn't any on this SoC, but a real terminal would say
// "0 KB" not omit the line), battery, SD/LittleFS status. Refreshes
// every draw. Kept intentionally minimal - a real REPL is out of scope
// for this restyle and would need parsing/history/etc.
class TerminalScreen : public Screen {
public:
    static TerminalScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "TERM"; }
    const char* helpText() const override {
        return "TERMINAL\n\nLive device readout.\nNot a real REPL.\nDEL: back to HOME";
    }
};
