#pragma once

#include "Screen.h"
#include <Arduino.h>

// "TARGET RANGE": type a base network address (e.g. 192.168.5.0) to make
// NETWORK SCAN sweep that /24 instead of the DHCP-detected subnet, or
// clear it to go back to the connected subnet. Reached with 'T' from
// NETWORK SCAN. See ScanManager::setScanRange (ARP/MAC only works on the
// local subnet; other subnets rely on routed L3 ping).
class TargetRangeScreen : public Screen {
public:
    static TargetRangeScreen& instance();

    void onEnter() override;
    void onExit() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "RNG"; }
    const char* helpText() const override {
        return "TARGET RANGE\nMENU>NET>T(RNG)\nType a base IP (e.g. 192.168.5.0)\nENTER: scan that /24\nC: clear (back to DHCP subnet)\nDEL: erase / back";
    }

private:
    String _input;
    String _status;
};
