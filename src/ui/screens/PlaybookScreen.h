#pragma once

#include "Screen.h"
#include <cstddef>

// "PLAYBOOK": pick one of a small library of scriptable, unattended scan
// sequences and run it start-to-finish with no further input. See
// scan/PlaybookRunner.h for the preset list and the reasoning behind
// each one. Reached from the main menu.
class PlaybookScreen : public Screen {
public:
    static PlaybookScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "PBK"; }
    const char* helpText() const override {
        return "PLAYBOOK\n\nScriptable scan sequences -\npick a preset, ENTER starts,\nwalk away. Arrows: pick\nENTER: start/stop\nDEL: back (keeps running)";
    }

private:
    void pushLog(const String& line);
    void drawPicker(M5Canvas& gfx);
    void drawRunning(M5Canvas& gfx);

    static constexpr uint8_t kLogLines = 5;

    size_t _selected = 0;
    String _log[kLogLines];
    uint8_t _logCount = 0;
};
