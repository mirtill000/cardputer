#pragma once

#include "Screen.h"
#include <vector>

// Simple bar chart of host-count-per-scan, built from the last
// ScanHistory::kMaxEntries snapshots (see storage/ScanHistory.h) -
// real data, not decorative like HostDetailScreen's radar. Read-only, a
// snapshot of what ScanHistory currently holds when this screen opens,
// not live-updating. Reachable from SCAN HISTORY with S.
class StatsScreen : public Screen {
public:
    static StatsScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

private:
    void drawChart(M5Canvas& gfx, int16_t top, int16_t bottom);

    struct Point {
        uint32_t seq = 0;
        size_t hostCount = 0;
        bool hadCritical = false;
    };

    std::vector<Point> _points;  // oldest first, for left-to-right chart order
    size_t _maxHostCount = 0;
};
