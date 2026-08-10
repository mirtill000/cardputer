#pragma once

#include "Screen.h"

struct MenuItem {
    const char* label;
    Screen* target;  // pushed onto the UI stack on Enter; null = disabled entry
};

// Top-level navigation hub. Deliberately knows nothing about which
// concrete screens exist — main.cpp wires up the MenuItem array at
// startup, so new modules (network scan, port scanner, ...) plug in
// without this file changing as they land in later development phases.
class MainMenuScreen : public Screen {
public:
    static MainMenuScreen& instance();

    void configure(const MenuItem* items, uint8_t count);

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    bool wantsRain() const override { return true; }
    uint8_t rainDensity() const override { return 4; }  // sparse/dim so menu text stays legible

private:
    const MenuItem* _items = nullptr;
    uint8_t _count = 0;
    uint8_t _selected = 0;
};
