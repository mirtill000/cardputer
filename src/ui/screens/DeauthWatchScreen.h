#pragma once

#include "Screen.h"

// "GUARD MODE": toggles DeauthWatcher's passive background watch on/off
// and lists every BSSID this session has seen deauth/disassoc frames
// referencing, with a running total and a live "flooding" flag once the
// rate crosses a real-attack-shaped threshold. Purely passive - never
// transmits; see scan/DeauthWatcher.h for the exact threshold/window and
// why isolated deauth frames (normal 802.11 housekeeping) don't trigger
// this.
class DeauthWatchScreen : public Screen {
public:
    static DeauthWatchScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "GRD"; }
    const char* helpText() const override {
        return "GUARD MODE\nMENU>NET>D>Ent(GRD)\nWatches for deauth/disassoc\nfloods - someone ELSE'S attack.\nt=total, /w=current 10s window\ncount. Red=flooding.\nENTER:start/stop\nArrows:move  DEL:back";
    }

private:
    void drawIncidents(M5Canvas& gfx, int16_t top);

    bool _running = false;
    size_t _selected = 0;
};
