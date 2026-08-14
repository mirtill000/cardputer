#pragma once

#include "Screen.h"
#include <Arduino.h>
#include <vector>

// "FILES": a small on-device browser for the export filesystem (SD, or
// LittleFS when no card) — lists files/dirs, enter directories, and delete
// files (with a confirm) so exports/reports/pcaps/wardrive logs don't just
// pile up with no way to manage them from the device. Reached with 'F'
// from SETTINGS.
class FileManagerScreen : public Screen {
public:
    static FileManagerScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* helpText() const override {
        return "FILES\n\narrows: move   ENTER: open dir\nX: delete selected file\nDEL: up a dir / back";
    }

private:
    struct Entry {
        String name;  // basename
        uint32_t size = 0;
        bool dir = false;
    };

    void rebuild();
    String fullPath(const String& base) const;

    String _path = "/";
    std::vector<Entry> _entries;
    size_t _selected = 0;
    bool _confirmDelete = false;
};
