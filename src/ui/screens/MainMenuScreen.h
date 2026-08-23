#pragma once

#include "Screen.h"

struct MenuItem {
    const char* label;
    Screen* target;  // pushed onto the UI stack on Enter; null = disabled entry
    // true = route through OffensiveDisclaimerScreen first, same gate
    // per-host offensive actions (MITM/deauth/PMKID/evil-twin) already
    // use, for a top-level entry that IS the offensive action itself
    // rather than a screen reached after picking a target elsewhere.
    bool offensive = false;
};

// Top-level navigation hub. Deliberately knows nothing about which
// concrete screens exist — main.cpp wires up the MenuItem array at
// startup, so new modules (network scan, port scanner, ...) plug in
// without this file changing as they land in later development phases.
class MainMenuScreen : public Screen {
public:
    static MainMenuScreen& instance();

    void configure(const MenuItem* items, uint8_t count);

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "WIFI"; }
    const char* helpText() const override {
        return "WIFI TOOLS\n\nAll WiFi-side tools in one\nplace. Arrows: move selection\nENTER: open highlighted tool\nDEL: back to HOME";
    }

private:
    const MenuItem* _items = nullptr;
    uint8_t _count = 0;
    uint8_t _selected = 0;
};
