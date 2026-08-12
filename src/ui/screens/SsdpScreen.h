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

private:
    void drawDevices(M5Canvas& gfx, int16_t top);

    size_t _selected = 0;
};
