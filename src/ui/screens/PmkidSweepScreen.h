#pragma once

#include "Screen.h"
#include <cstddef>

// "PMKID SWEEP": drives PmkidSweepManager - runs PMKID CAPTURE against
// every non-open AP WAR DRIVING already knows about, one after another,
// instead of picking a single sighting and pressing P each time.
// Reached from WardrivingScreen ('S'), gated by the same offensive
// disclaimer as EVIL TWIN/DEAUTH/single-AP PMKID CAPTURE.
class PmkidSweepScreen : public Screen {
public:
    static PmkidSweepScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "PSWP"; }
    const char* helpText() const override {
        return "PMKID SWEEP\nMENU>WD>S(PSWP)\nBefore start: preview of\neligible targets + RSSI.\nRuns PMKID CAPTURE against\neach in turn, no deauth.\nI: result detail\nENTER: start/stop   DEL: back";
    }

private:
    void drawResults(M5Canvas& gfx, int16_t top);
    void drawPreview(M5Canvas& gfx, int16_t top);

    size_t _selected = 0;
    bool _showDetail = false;
};
