#pragma once

#include "Screen.h"

// "DIAGNOSTICS": a device-health self-test — SD, WiFi, battery, IMU,
// keyboard and speaker — handy to run before an engagement. Read-only
// except the speaker test ('S') and the keyboard echo. Reached with 'D'
// from SETTINGS.
class DiagnosticsScreen : public Screen {
public:
    static DiagnosticsScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "DIAG"; }
    const char* helpText() const override {
        return "DIAGNOSTICS\nMENU>SET>D(DIAG)\nDevice self-test.\nS: play a speaker test tone\nType any key: keyboard echo\nDEL: back";
    }

private:
    char _lastKey = 0;
};
