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

private:
    void drawResponders(M5Canvas& gfx, int16_t top);

    size_t _selected = 0;
};
