#pragma once

#include "Screen.h"
#include <Arduino.h>
#include <vector>

// "FILES": a small on-device browser for the export filesystem (SD, or
// LittleFS when no card) — lists files/dirs, enter directories, and delete
// files (with a confirm) so exports/reports/pcaps/wardrive logs don't just
// pile up with no way to manage them from the device. Reached with 'F'
// from SETTINGS.
//
// N/H jump straight to /netrunner and /handshakes (Fase 37) - almost
// every artifact this firmware produces (scan reports, wardrive.csv,
// PMKID/deauth/sentinel pcaps + summaries) lands in one of those two
// folders, so having to manually drill down from / every single visit
// was pure friction. Safe to press even before either folder exists
// yet (a fresh device, or before any capture has run): rebuild() already
// degrades to the normal "empty directory" empty-state, same as
// navigating there by hand would.
class FileManagerScreen : public Screen {
public:
    static FileManagerScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "FILES"; }
    const char* helpText() const override {
        return "FILES\nMENU>SET>F(FILES)\narrows: move   ENTER: open dir\nX: delete selected file\nN: jump to /netrunner\nH: jump to /handshakes\nDEL: up a dir / back";
    }

private:
    struct Entry {
        String name;  // basename
        uint32_t size = 0;
        bool dir = false;
    };

    void rebuild();
    String fullPath(const String& base) const;
    void jumpTo(const String& path);

    String _path = "/";
    std::vector<Entry> _entries;
    size_t _selected = 0;
    bool _confirmDelete = false;
};
