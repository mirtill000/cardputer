#pragma once

#include "Screen.h"
#include <cstdint>

// "CHANNEL SCAN": live 2.4GHz channel-congestion chart (1-13), one bar
// per channel showing how many APs are currently broadcasting there -
// the same "which channel is quietest" question a phone WiFi-analyzer
// app answers, plus your own AP's channel marked for comparison.
//
// Drives its own continuous WiFi scan loop, same pattern as
// SignalFinderScreen (see its header for why: WardrivingManager's own
// scan cadence is too slow for a live view) - reuses WifiManager's
// beginScan()/scanStatus()/getScanResult(). Competes for the single
// radio with WAR DRIVING/WIFI SCAN/SignalFinder the same way those
// already compete with each other.
class ChannelScanScreen : public Screen {
public:
    static ChannelScanScreen& instance();

    void onEnter() override;
    void update(uint32_t nowMs) override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "CHAN"; }
    const char* helpText() const override {
        return "CHANNEL SCAN\n\nLive AP count per 2.4GHz\nchannel - taller/redder bar =\nmore crowded. Cyan mark above\na bar = your own AP's channel.\nLeft/Right: select channel\nDEL: back";
    }

private:
    static constexpr uint8_t kChannels = 13;

    uint16_t _apCount[kChannels] = {0};
    int32_t _rssiSum[kChannels] = {0};
    uint8_t _selected = 0;
    uint32_t _scanCount = 0;
};
