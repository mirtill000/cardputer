#pragma once

#include "Screen.h"
#include <Arduino.h>
#include <cstddef>

// "PASSWORD SPRAY": one password tried across many discovered hosts and a
// small username list, deliberately paced to STAY UNDER lockout thresholds
// (opposite of a per-user brute). Real login attempts, gated by the same
// AUTHORIZATION consent as CREDENTIAL AUDIT / IOT CREDS.
class PasswordSprayScreen : public Screen {
public:
    static PasswordSprayScreen& instance();

    void onEnter() override;
    void onExit() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "SPRAY"; }
    const char* helpText() const override {
        return "PASSWORD SPRAY\n\nOne password vs many hosts,\nlockout-safe pacing.\nType password ENTER: run\nDEL:back (or clear pw)";
    }

private:
    void drawList(M5Canvas& gfx, int16_t top);
    bool _consented = false;
    String _password;
    bool _typing = true;
    size_t _selected = 0;
};
