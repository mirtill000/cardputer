#pragma once

#include "Screen.h"

// "Not implemented yet" screen used for menu entries whose real module
// lands in a later development phase. Once a module ships, main.cpp
// simply points that MenuItem at the real screen instead — nothing else
// changes.
class PlaceholderScreen : public Screen {
public:
    void configure(const char* title, const char* description);

    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

private:
    const char* _title = "";
    const char* _description = "";
};
