#pragma once

#include "Screen.h"

// Splash screen: full-screen Matrix rain background, a scripted boot log
// that "types" itself in, then a glitch-in title and a blinking prompt.
// Purely cosmetic at this stage (see README roadmap) — later phases can
// wire the log lines to real subsystem init results if desired.
class BootScreen : public Screen {
public:
    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void update(uint32_t nowMs) override;
    void draw(M5Canvas& gfx) override;

    bool wantsRain() const override { return true; }
    uint8_t rainDensity() const override { return 16; }

private:
    uint32_t _enterMs = 0;
    uint8_t _linesShown = 0;
    bool _titleShown = false;
    bool _promptShown = false;
};
