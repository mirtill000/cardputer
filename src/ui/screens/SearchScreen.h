#pragma once

#include "Screen.h"
#include <Arduino.h>
#include <vector>
#include <cstddef>

// "SEARCH": free-text lookup across the current host table by IP / MAC /
// vendor / hostname. Type to filter live; ENTER switches to browsing the
// matches (arrows + ENTER to open a host's detail). Reached with '/' from
// NETWORK SCAN.
class SearchScreen : public Screen {
public:
    static SearchScreen& instance();

    void onEnter() override;
    void onExit() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* helpText() const override {
        return "SEARCH\n\nType to filter hosts by\nIP / MAC / vendor / hostname.\nENTER: browse matches\n(in browse) ENTER: open host\nDEL: erase / back";
    }

private:
    void rebuild();

    String _query;
    std::vector<size_t> _results;  // indices into ScanManager's host table
    size_t _selected = 0;
    bool _browsing = false;
};
