#pragma once

#include "Screen.h"

// "ABOUT" - firmware version, hardware target, project short blurb,
// credits. Reached with 'A' from HomeScreen (Fase 54). Read-only.
class AboutScreen : public Screen {
public:
    static AboutScreen& instance();

    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "ABOUT"; }
    const char* helpText() const override {
        return "ABOUT\n\nFirmware / hardware info.\nDEL: back to HOME";
    }
};
