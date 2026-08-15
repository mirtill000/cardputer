#pragma once

#include "Screen.h"

// "UPNP DISCOVERY": drives SsdpDiscovery's one-shot M-SEARCH sweep and
// lists whatever answers (smart TVs, NAS, routers, IoT gear).
class SsdpScreen : public Screen {
public:
    static SsdpScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* helpText() const override {
        return "UPNP DISCOVERY\n\nOne M-SEARCH multicast -\nlists whatever answers (TVs,\nNAS, routers, IoT).\nENTER: sweep   I: full detail\nArrows: move   DEL: back";
    }

private:
    void drawDevices(M5Canvas& gfx, int16_t top);

    size_t _selected = 0;
    bool _showDetail = false;
};
