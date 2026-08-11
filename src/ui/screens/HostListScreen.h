#pragma once

#include "Screen.h"
#include <IPAddress.h>
#include <vector>
#include <cstddef>

// The "NETWORK SCAN" dashboard: drives the discovery sweep (WiFi
// connection itself is WifiSetupScreen's job, kicked off at boot with
// any saved credentials — see main.cpp) and shows discovered hosts as a
// hacker-terminal-style table (IP / vendor / class), color-coded by
// risk. Deliberately shows only hosts confirmed alive — a home/office
// subnet scan is mostly silence, and a wall of "no response" rows would
// bury the interesting rows.
class HostListScreen : public Screen {
public:
    static HostListScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

private:
    void rebuildAliveList();
    void drawTable(M5Canvas& gfx, int16_t top);
    bool isNewHost(const IPAddress& ip) const;
    bool isNeverSeenBefore(const IPAddress& ip) const;

    std::vector<size_t> _aliveIndices;
    size_t _selected = 0;
    String _statusLine;  // transient feedback, e.g. after an export (E)
    uint32_t _scanStartMs = 0;
    uint32_t _scanFinishMs = 0;  // valid once a scan has completed at least once
    std::vector<IPAddress> _newHostIps;  // set after ScanFinished - see storage/ScanHistory.h
    std::vector<IPAddress> _neverSeenIps;  // set after ScanFinished - MAC not in any past scan of this network
};
