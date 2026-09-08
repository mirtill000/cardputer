#pragma once

#include "Screen.h"
#include <cstddef>

// "THREATS": the on-device view of the app's unified finding rollup.
// It renders exactly what FindingStore::buildRollup() returns (see
// scan/FindingStore.h) — the SAME list the HTML report's ATTACK SURFACE
// section and the JSON export now emit, so all three agree. That rollup
// merges default-credential hits and known-vulnerable banners and
// plaintext services (from ScanManager's host table), suspicious rogue
// DHCP, WPS-unlocked APs, deauth floods (GUARD + SENTINEL), new/gone
// devices, unauthenticated MQTT/Modbus/CoAP/BACnet/DNP3 endpoints, a
// PMKID-sweep status note, and anything any module pushed via
// FindingStore::add(). Read-only; rebuilt from live data every draw.
class ThreatsScreen : public Screen {
public:
    static ThreatsScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "THRT"; }
    const char* helpText() const override {
        return "THREATS\n\nLive rollup of the worst\nfindings across every module -\ndefault creds, plaintext\nservices, rogue DHCP.\nI: full text\nArrows: move   DEL: back";
    }

private:
    size_t _selected = 0;
    bool _showDetail = false;
};
