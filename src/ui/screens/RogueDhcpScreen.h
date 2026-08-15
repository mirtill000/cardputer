#pragma once

#include "Screen.h"

// "ROGUE DHCP": toggles RogueDhcpDetector's passive background watch
// on/off and lists every DHCP server seen answering on the wire this
// session. Any server whose IP differs from this device's own gateway
// is flagged in red as a possible rogue. Purely passive — it never
// answers DHCP itself; see scan/RogueDhcpDetector.h for the real
// limitation (only sees traffic on OPEN networks, since WPA-encrypted
// frames aren't readable in promiscuous mode).
class RogueDhcpScreen : public Screen {
public:
    static RogueDhcpScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* helpText() const override {
        return "ROGUE DHCP\n\nWatches for DHCP servers on\nthe wire; red = IP differs\nfrom your own gateway.\nENTER: start/stop\nArrows: move   DEL: back";
    }

private:
    void drawSightings(M5Canvas& gfx, int16_t top);

    bool _running = false;
    size_t _selected = 0;
};
