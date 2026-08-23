#pragma once

#include "Screen.h"
#include <cstddef>

// "BLE SCAN" - inventory dashboard for the Fase 52 BluetoothManager.
// Each row: addr / RSSI / vendor / short vendor/beacon/tracker/HID tag.
// A "W" marker on the right edge means "vendor also seen on the WiFi
// host table" (correlation feature #10).
// ENTER opens the per-device detail screen (Continuity/beacon parsing).
// 'T' filters to trackers-only (feature #7).
class BleScannerScreen : public Screen {
public:
    static BleScannerScreen& instance();

    void onEnter() override;
    void onExit() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "BLE"; }
    const char* helpText() const override {
        return "BLE SCAN\n\nPassive BLE inventory + vendor\n+ tracker + WiFi correlation.\nENTER: detail   T: trackers\nH: HID devices  G: GATT walk\nS: start/stop   DEL: back";
    }

private:
    void drawList(M5Canvas& gfx, int16_t top);
    size_t _selected = 0;
};
