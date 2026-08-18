#include "StatsScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../storage/SdCard.h"
#include "../../storage/ScanHistory.h"

StatsScreen& StatsScreen::instance() {
    static StatsScreen s;
    return s;
}

void StatsScreen::onEnter() {
    _points.clear();
    _maxHostCount = 0;

    fs::FS& fs = sdcard::exportFs();
    std::vector<ScanHistory::HistoryEntry> entries;
    ScanHistory::listEntries(fs, entries);  // newest first

    // Reversed into oldest-first order so the chart reads left-to-right
    // as "further back in time" -> "now", the usual convention for a
    // trend chart.
    for (size_t i = entries.size(); i-- > 0;) {
        const auto& e = entries[i];
        Point p;
        p.seq = e.seq;
        p.hostCount = e.hostCount;

        std::vector<ScanHistory::HistoryHostSnapshot> hosts;
        if (ScanHistory::loadEntry(fs, e.filename, hosts)) {
            for (const auto& h : hosts) {
                if (h.risk == "critical") {
                    p.hadCritical = true;
                    break;
                }
            }
        }

        if (p.hostCount > _maxHostCount) _maxHostCount = p.hostCount;
        _points.push_back(p);
    }
}

void StatsScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Back) g_ui.popScreen();
}

void StatsScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "STATS");

    if (_points.empty()) {
        gfx.setTextColor(theme::AMBER, theme::BG);
        gfx.setCursor(6, 30);
        gfx.print("no scans yet - run NETWORK SCAN first");
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(4, gfx.height() - 9);
        gfx.print("DEL:back");
        return;
    }

    size_t total = 0;
    for (const auto& p : _points) total += p.hostCount;
    float avg = (float)total / (float)_points.size();

    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("SCANS: ");
    gfx.print((unsigned)_points.size());
    gfx.print("  AVG HOSTS: ");
    gfx.print(avg, 1);

    // Trend: average of the newer half vs. the older half - simple and
    // honest about being coarse (not a real regression fit), which is
    // all a handful of data points on a 240px chart warrants anyway.
    if (_points.size() >= 4) {
        size_t half = _points.size() / 2;
        float firstAvg = 0, secondAvg = 0;
        for (size_t i = 0; i < half; i++) firstAvg += (float)_points[i].hostCount;
        for (size_t i = half; i < _points.size(); i++) secondAvg += (float)_points[i].hostCount;
        firstAvg /= (float)half;
        secondAvg /= (float)(_points.size() - half);

        const char* label = "FLAT";
        uint16_t color = theme::GREY;
        if (secondAvg > firstAvg + 0.5f) {
            label = "UP";
            color = theme::GREEN;
        } else if (secondAvg < firstAvg - 0.5f) {
            label = "DOWN";
            color = theme::AMBER;
        }
        gfx.setTextColor(color, theme::BG);
        gfx.setCursor(6, 28);
        gfx.print("TREND: ");
        gfx.print(label);
    }

    drawChart(gfx, 40, 110);

    // Red bars = a critical-risk host was found that scan (same color
    // convention as NETWORK SCAN/PORT MAPPING) - no room for a spelled-
    // out legend on top of the chart+summary above, and the color
    // already carries the meaning everywhere else in this app.
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back");
}

void StatsScreen::drawChart(M5Canvas& gfx, int16_t top, int16_t bottom) {
    gfx.drawFastHLine(8, bottom, gfx.width() - 16, theme::GREY);

    size_t n = _points.size();
    if (n == 0) return;

    int16_t chartW = gfx.width() - 20;
    int16_t chartH = bottom - top;
    float slot = (float)chartW / (float)n;
    int16_t barW = (int16_t)slot - 1;
    if (barW < 1) barW = 1;

    size_t maxCount = (_maxHostCount > 0) ? _maxHostCount : 1;

    for (size_t i = 0; i < n; i++) {
        const auto& p = _points[i];
        int16_t x = 10 + (int16_t)((float)i * slot);

        int16_t barH = (int16_t)((float)p.hostCount / (float)maxCount * (float)chartH);
        if (p.hostCount > 0 && barH < 1) barH = 1;  // stays visible even for a single host on a tall chart

        uint16_t color = p.hadCritical ? theme::RED : theme::CYAN;
        if (barH > 0) gfx.fillRect(x, bottom - barH, barW, barH, color);

        // Marks the most recent scan distinctly, since it's the one
        // that matters most for "where do things stand right now".
        if (i == n - 1) {
            gfx.drawRect(x - 1, bottom - barH - 1, barW + 2, barH + 2, theme::MAGENTA);
        }
    }
}
