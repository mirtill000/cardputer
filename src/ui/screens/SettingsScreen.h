#pragma once

#include "Screen.h"
#include <cstdint>

// Real settings editor (replaces the old placeholder). Edits AppConfig
// fields directly in RAM as the user adjusts them (so a change takes
// effect on the very next scan without leaving this screen), and
// persists to NVS once on the way out — not on every keypress, to
// avoid hammering flash.
class SettingsScreen : public Screen {
public:
    static SettingsScreen& instance();

    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* helpText() const override {
        return "SETTINGS\n\narrows: select / adjust value\nO: OTA firmware update\nB: backup config to SD\nR: restore config from SD\nF: SD file manager\nD: hardware diagnostics\nENTER/DEL: save & exit";
    }

private:
    void adjust(int direction);

    static constexpr uint8_t kFieldCount = 8;
    uint8_t _selected = 0;
    String _statusLine;  // transient feedback after B (backup) / R (restore)
};
