#pragma once

#include "Screen.h"

// "VLAN HOP": toggles VlanHopProbe's passive 802.1Q tag-leak listen,
// and offers a best-effort active double-tagging probe (P, gated by
// OffensiveDisclaimerScreen) with two adjustable VLAN ID fields. See
// scan/VlanHopProbe.h for the real hardware limitation this works
// under (WiFi station, not a wired switch port) and what a "sent"
// result does and doesn't prove.
class VlanHopScreen : public Screen {
public:
    static VlanHopScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "VLAN"; }
    const char* helpText() const override {
        return "VLAN HOP\nMENU>NET>D>Ent(VLAN)\nENTER:leak-listen start/stop\nTAB:field </>:adjust VLAN ID\nP:send double-tag probe (gated)\nI:detail  Arrows:move\nDEL:back (open networks only)";
    }

private:
    void drawSightings(M5Canvas& gfx, int16_t top);

    bool _running = false;
    size_t _selected = 0;
    bool _showDetail = false;
    bool _nativeFieldFocused = true;  // false = target field focused
    uint16_t _nativeVlanId = 1;
    uint16_t _targetVlanId = 10;
    String _log[3];
    uint8_t _logCount = 0;
};
