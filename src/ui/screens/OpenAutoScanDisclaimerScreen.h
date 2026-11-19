#pragma once

#include "Screen.h"

// Gate in front of WAR DRIVING's "auto-scan open networks" mode. That
// mode makes the wardrive actively JOIN every open access point it sees
// and run a discovery + well-known-port scan against it - not just the
// ones the user put on the allowlist. Connecting to and scanning a
// network you were not specifically authorized for is exactly what the
// allowlist exists to scope, so the warning below is shown before it can
// be turned on.
//
// Confirming (Y) sets AppConfig::wardriveAutoScanOpen, which is PERSISTED
// across reboots (user's choice), and pops back to whatever pushed this
// screen (the WAR DRIVING screen). Declining (DEL) just pops without
// enabling anything.
class OpenAutoScanDisclaimerScreen : public Screen {
public:
    static OpenAutoScanDisclaimerScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "AUTH"; }
    const char* helpText() const override {
        return "AUTO-SCAN OPEN NETWORKS\n\nJoins and scans EVERY open AP,\nnot just allowlisted ones.\nStays on across reboots.\nY: confirm   DEL: cancel";
    }
};
