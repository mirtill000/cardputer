#pragma once

#include "Screen.h"
#include <cstddef>

// "SENTINEL MODE": toggles SentinelManager's continuous watch of the
// currently-connected network on/off - periodic re-discovery with a
// sound alert on any never-before-seen OR now-missing device, deauth/
// disassoc flood detection folded in, and a running traffic dump
// (802.11 frame headers, management+data) to rotating .pcap files on
// SD. See scan/SentinelManager.h for exactly what each event means and
// why the traffic dump can't and doesn't decrypt anything.
class SentinelScreen : public Screen {
public:
    static SentinelScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "SENT"; }
    const char* helpText() const override {
        return "SENTINEL MODE\nWatches connected net: new/\nmissing device alert, deauth\nflood detect, traffic dump to\nrotating .pcap, summary on stop\nI:event detail\nENTER:start/stop  Arrows:move\nDEL:back";
    }

private:
    void drawEvents(M5Canvas& gfx, int16_t top);

    size_t _selected = 0;
    bool _showDetail = false;
};
