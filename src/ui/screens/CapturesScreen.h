#pragma once

#include "Screen.h"
#include <Arduino.h>
#include <vector>

// "CAPTURES": a unified read-only browser for every .pcap artifact this
// firmware can produce (PMKID SWEEP + single-AP PMKID CAPTURE +
// GUARD MODE/DEAUTH under /handshakes, SENTINEL MODE's rotated dumps
// under /netrunner), so there's one place to see what's been captured
// across sessions instead of having to remember which folder which tool
// wrote to. Delete-only management here (rename/move isn't meaningful
// for a capture) — full filesystem browsing for anything else still
// lives in FILE MANAGER (F key, SETTINGS). Reached with 'C' from
// SETTINGS.
class CapturesScreen : public Screen {
public:
    static CapturesScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "CAPS"; }
    const char* helpText() const override {
        return "CAPTURES\nMENU>SET>C(CAPS)\nEvery .pcap this firmware\nhas written (handshakes +\nsentinel), in one list.\nArrows: move   I: detail\nX: delete   DEL: back";
    }

private:
    struct Entry {
        String path;  // full path, e.g. /handshakes/pmkid_xx.pcap
        String name;  // basename shown in the list
        uint32_t size = 0;
    };

    void rebuild();
    void scanDir(const char* dir);

    std::vector<Entry> _entries;
    size_t _selected = 0;
    bool _confirmDelete = false;
    bool _showDetail = false;
};
