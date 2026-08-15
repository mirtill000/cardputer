#pragma once

#include "Screen.h"

// "SNMP SWEEP": runs SnmpSweep over the alive-host list and shows which
// hosts answer the "public" community, with their sysDescr. See
// scan/SnmpSweep.h — read-only GET of sysDescr.0, nothing more.
class SnmpScreen : public Screen {
public:
    static SnmpScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* helpText() const override {
        return "SNMP SWEEP\n\nGETs sysDescr.0 with community\n'public' from every host -\nread-only, never SET.\nENTER: sweep   I: full sysDescr\nArrows: move   DEL: back";
    }

private:
    void drawResponders(M5Canvas& gfx, int16_t top);

    size_t _selected = 0;
    bool _showDetail = false;
};
