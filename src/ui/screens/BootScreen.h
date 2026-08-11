#pragma once

#include "Screen.h"

// Splash screen: a scripted boot log that "types" itself in, then hands
// off to a branded NETRUNNER dashboard view (title, subtitle, version,
// skyline, status bar — matching MainMenuScreen's visual language) with
// a blinking "PRESS ENTER" prompt. No Matrix rain (removed for
// simplicity while getting a stable baseline working; see README).
class BootScreen : public Screen {
public:
    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void update(uint32_t nowMs) override;
    void draw(M5Canvas& gfx) override;

private:
    uint32_t _enterMs = 0;
    uint8_t _linesShown = 0;
    bool _titleShown = false;
    bool _promptShown = false;
};
