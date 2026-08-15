#pragma once

#include "Screen.h"
#include <cstddef>

// "THREATS": a single live view that aggregates the standout findings
// scattered across the other modules — default-credential hits and
// known-vulnerable banners (from ScanManager's host table), plaintext
// services (telnet/ftp), suspicious rogue-DHCP servers (from
// RogueDhcpDetector), APs with WPS enabled and unlocked (from
// BeaconProbeSniffer), and deauth/disassoc floods in progress (from
// DeauthWatcher/GUARD MODE). Read-only; it re-derives the list from the
// live data on every draw, so it reflects whatever the background
// scanners have found so far. It's the on-device counterpart to the
// report's ATTACK SURFACE section.
class ThreatsScreen : public Screen {
public:
    static ThreatsScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* helpText() const override {
        return "THREATS\n\nLive rollup of the worst\nfindings across every module -\ndefault creds, plaintext\nservices, rogue DHCP.\nI: full text\nArrows: move   DEL: back";
    }

private:
    size_t _selected = 0;
    bool _showDetail = false;
};
