#pragma once

#include "Screen.h"
#include <cstddef>

// "BLUETOOTH TOOLS" submenu (Fase 56), reached from HomeScreen's BT tile.
// Groups every BLE-side tool - BLE SCAN, BLE HID, BLE TRACKERS - in one
// place so BLE has parity with the WIFI TOOLS submenu. Individual GATT
// walks are still reached from within BLE SCAN / BLE HID.
class BluetoothToolsMenuScreen : public Screen {
public:
    static BluetoothToolsMenuScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "BT"; }
    const char* helpText() const override {
        return "BLUETOOTH TOOLS\n\nAll BLE tools in one place.\nArrows: move  ENTER: open\nDEL: back to HOME";
    }

private:
    size_t _selected = 0;
};
