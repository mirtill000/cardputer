#pragma once

#include "Screen.h"

// "PASSIVE HOSTS": toggles PassiveHostDiscovery's background listen and
// lists every (IP, MAC) pair overheard on the segment — hosts learned
// without sending a single probe. See scan/PassiveHostDiscovery.h for
// the open-network-only / shared-radio caveats.
class PassiveHostScreen : public Screen {
public:
    static PassiveHostScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

private:
    void drawHosts(M5Canvas& gfx, int16_t top);

    bool _running = false;
    size_t _selected = 0;
};
