#pragma once

#include "Screen.h"
#include <cstddef>

// "BLE TRACKERS" - feature #7: shows only devices whose advertising data
// matches a known unwanted-tracker pattern (Apple FindMy / AirTag, Tile,
// Samsung SmartTag). Devices are still listed by BluetoothManager and
// visible in BLE SCAN; this is just a filtered dashboard useful to spot
// a persistent tracker in a room. Reached with 'T' from BLE SCAN.
class BleTrackerScreen : public Screen {
public:
    static BleTrackerScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "BLE-TRK"; }
    const char* helpText() const override {
        return "BLE TRACKERS\n\nAirTag / Tile / SmartTag ads\nseen in this session.\nENTER: detail  DEL: back";
    }

private:
    size_t _selected = 0;
};
