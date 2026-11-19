#pragma once

#include "Screen.h"

// Gate in front of WAR DRIVING's "auto-scan open networks" mode. That
// mode makes the wardrive actively JOIN every open access point it sees
// and run a discovery + well-known-port scan against it - not just the
// ones the user put on the allowlist. Connecting to and scanning a
// network you were not specifically authorized for is exactly what the
// allowlist exists to scope, so turning this blanket mode on is held to
// the same bar as the offensive tools: the word AUTHORIZED typed in full.
//
// Accepting sets AppConfig::wardriveAutoScanOpen for the rest of this
// session only (never persisted - every boot starts back at off) and pops
// back to whatever pushed this screen (the WAR DRIVING screen). Declining
// (DEL on an empty field) just pops without enabling anything.
class OpenAutoScanDisclaimerScreen : public Screen {
public:
    static OpenAutoScanDisclaimerScreen& instance();

    void onEnter() override;
    void onExit() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "AUTH"; }
    const char* helpText() const override {
        return "AUTO-SCAN OPEN NETWORKS\n\nJoins and scans EVERY open AP,\nnot just allowlisted ones.\nType AUTHORIZED in full,\nthen ENTER to enable.\nDEL: erase / cancel";
    }

private:
    String _typed;
};
