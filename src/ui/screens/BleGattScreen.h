#pragma once

#include "Screen.h"
#include <Arduino.h>
#include <cstddef>

// "BLE GATT" - runs BleGattClient against a chosen device and shows the
// walk result: DIS strings (manufacturer/model/firmware), posture stats
// (writable no-auth count, HID service seen), and any known control-
// vector hits (feature #8) highlighted in red. Reached with 'G' from
// BLE DETAIL / BLE HID. Real GATT connection under the hood - gated by
// the same AppConfig::credAuditEnabled consent used everywhere active
// in this firmware.
class BleGattScreen : public Screen {
public:
    static BleGattScreen& instance();

    void setTarget(const String& addr) { _target = addr; }

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "GATT"; }
    const char* helpText() const override {
        return "BLE GATT WALK\n\nConnects and enumerates\nservices/characteristics.\nDetection-only (never writes).\nENTER: (re)run   DEL: back";
    }

private:
    bool _consented = false;
    String _target;
    size_t _selected = 0;
};
