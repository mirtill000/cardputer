#pragma once

#include "Screen.h"
#include <cstddef>

// "ACTIVITY": the full-screen expansion of the header's compact RF:/BG:
// tag (see ui/ActivityStatus.h) - every long-running manager this
// firmware tracks, listed with its current running state, instead of
// just the one name-plus-count the header has room for. Read-only: this
// screen starts/stops nothing itself, it only shows what's already
// running so nothing gets left going in the background unnoticed.
// Reached from the main menu.
class ActivityScreen : public Screen {
public:
    static ActivityScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "ACT"; }
    const char* helpText() const override {
        return "ACTIVITY\n\nEvery background task this\nfirmware tracks, running or\nnot. RF = uses the radio\n(only one at a time is safe).\nArrows: move   DEL: back";
    }

private:
    size_t _selected = 0;
};
