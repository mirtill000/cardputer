#pragma once

#include "Screen.h"

// "EAP IDENTITY": toggles EapIdentityHarvester's passive listen and
// lists every WPA-Enterprise supplicant caught disclosing its cleartext
// outer identity (username) before the PEAP/TTLS tunnel comes up. See
// scan/EapIdentityHarvester.h for the on-channel limitation and why an
// empty list usually just means no 802.1X auth happened on this
// channel while listening, not that the parser failed.
class EapIdentityScreen : public Screen {
public:
    static EapIdentityScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "EAP"; }
    const char* helpText() const override {
        return "EAP IDENTITY\nMENU>NET>D>Ent(EAP)\nHarvests cleartext 802.1X\nusernames (outer identity) -\nno probes sent. I:detail\nArrows:move  ENTER:start/stop\nDEL:back (on STA channel only)";
    }

private:
    void drawRows(M5Canvas& gfx, int16_t top);

    bool _running = false;
    size_t _selected = 0;
    bool _showDetail = false;
};
