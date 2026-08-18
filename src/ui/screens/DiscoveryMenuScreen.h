#pragma once

#include "Screen.h"
#include <cstddef>

// "DISCOVERY": a submenu that gathers every network-discovery tool in one
// place, reached with 'D' from NETWORK SCAN. These used to be scattered as
// top-level main-menu entries; grouping them under NETWORK SCAN keeps all
// discovery in a single spot and declutters the main menu. Each entry just
// opens the corresponding discovery screen.
class DiscoveryMenuScreen : public Screen {
public:
    static DiscoveryMenuScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "DISC"; }
    const char* helpText() const override {
        return "DISCOVERY\nMENU>NET>D(DISC)\nAll discovery tools, one place.\nArrows:move  ENTER:open\nDEL:back to NETWORK SCAN\nDot: green=ready, red=waiting\non its NETWORK/PORT SCAN.";
    }

private:
    size_t _selected = 0;
};
