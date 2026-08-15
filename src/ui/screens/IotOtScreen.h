#pragma once

#include "Screen.h"

// "IOT/OT SWEEP": runs IotOtProbe over the alive-host list and lists
// unauthenticated MQTT/Modbus/CoAP endpoints found. See scan/IotOtProbe.h
// — read-only detection, same risk tier as DATASTORE SWEEP.
class IotOtScreen : public Screen {
public:
    static IotOtScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* helpText() const override {
        return "IOT/OT SWEEP\n\nChecks MQTT/Modbus/CoAP for\nno-auth access on every host.\nENTER: sweep   I: full detail\nArrows: move   DEL: back";
    }

private:
    void drawFindings(M5Canvas& gfx, int16_t top);

    size_t _selected = 0;
    bool _showDetail = false;
};
