#pragma once

#include "Screen.h"
#include <Arduino.h>

// Per-BLE-device detail: full address, addr kind, RSSI, name, appearance,
// vendor (from company-ID lookup or beacon fingerprint), platform note
// (Apple Continuity / Fast Pair / Swift Pair / HID hint), beacon summary
// (iBeacon/Eddystone/AltBeacon), tracker note (AirTag/Tile/SmartTag),
// WiFi correlation (feature #10). Reached by ENTER from BleScannerScreen.
class BleDetailScreen : public Screen {
public:
    static BleDetailScreen& instance();

    void setAddress(const String& addr) { _addr = addr; }

    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "BLE-DET"; }
    const char* helpText() const override {
        return "BLE DETAIL\n\nFull parsing for one device:\naddr / vendor / continuity /\nbeacon / tracker / WiFi\ncorrelation.\nDEL: back";
    }

private:
    String _addr;
};
