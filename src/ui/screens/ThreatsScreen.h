#pragma once

#include "Screen.h"
#include <cstddef>

// "THREATS": a single live view that aggregates the standout findings
// scattered across the other modules — default-credential hits and
// known-vulnerable banners (from ScanManager's host table), plaintext
// services (telnet/ftp), and suspicious rogue-DHCP servers (from
// RogueDhcpDetector). Read-only; it re-derives the list from the live
// data on every draw, so it reflects whatever the background scanners
// have found so far. It's the on-device counterpart to the report's
// ATTACK SURFACE section.
class ThreatsScreen : public Screen {
public:
    static ThreatsScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

private:
    size_t _selected = 0;
};
