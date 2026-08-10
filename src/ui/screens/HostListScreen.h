#pragma once

#include "Screen.h"
#include <vector>
#include <cstddef>

// The "NETWORK SCAN" dashboard: drives WiFi connect + the discovery
// sweep, and shows discovered hosts as a hacker-terminal-style table
// (IP / vendor / class), color-coded by risk. Deliberately shows only
// hosts confirmed alive — a home/office subnet scan is mostly silence,
// and a wall of "no response" rows would bury the interesting rows.
class HostListScreen : public Screen {
public:
    static HostListScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void update(uint32_t nowMs) override;
    void draw(M5Canvas& gfx) override;

private:
    void rebuildAliveList();
    void drawTable(M5Canvas& gfx, int16_t top);

    std::vector<size_t> _aliveIndices;
    size_t _selected = 0;
    bool _wifiConnectTriggered = false;
};
