#pragma once

#include "Screen.h"
#include <vector>
#include "../../storage/ScanHistory.h"

// Browsable list of past discovery scans (see storage/ScanHistory.h),
// newest first. Read-only - this screen never re-runs anything, it just
// shows what a scan looked like at the time it finished. Reachable from
// MAIN MENU as "SCAN HISTORY".
class ScanHistoryScreen : public Screen {
public:
    static ScanHistoryScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "HIST"; }
    const char* helpText() const override {
        return "SCAN HISTORY\n\nBrowse past NETWORK SCAN\nsnapshots.\nENTER: open   S: stats chart\nArrows: move   DEL: back";
    }

private:
    enum class State { List, Detail };

    void drawList(M5Canvas& gfx, int16_t top);
    void drawDetail(M5Canvas& gfx, int16_t top);
    void openSelected();

    State _state = State::List;
    std::vector<ScanHistory::HistoryEntry> _entries;
    std::vector<ScanHistory::HistoryHostSnapshot> _detailHosts;
    size_t _selected = 0;
    size_t _detailSelected = 0;
};
