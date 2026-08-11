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

private:
    void adjust(int direction);

    static constexpr uint8_t kFieldCount = 6;
    uint8_t _selected = 0;
};
