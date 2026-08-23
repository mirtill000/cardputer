#pragma once

#include "Screen.h"
#include <cstddef>

// "BLE HID" - filtered view of BluetoothManager's device table showing
// ONLY advertisers that expose the HID service (0x1812) in their ad.
// Wireless keyboards and mice are the interesting entries here - a
// keyboard within a room is a real injection risk vector. Reached with
// 'H' from BLE SCAN. ENTER on a device opens BleGattScreen (feature #2
// walk, still gated) to look at the actual HID Report Map / auth
// posture.
class BleHidScreen : public Screen {
public:
    static BleHidScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "BLE-HID"; }
    const char* helpText() const override {
        return "BLE HID\n\nKeyboards/mice/gamepads that\nadvertise the HID service.\nENTER: GATT walk (gated)\nDEL: back";
    }

private:
    size_t _selected = 0;
};
