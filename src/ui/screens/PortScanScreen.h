#pragma once

#include "Screen.h"
#include <IPAddress.h>
#include <cstddef>

// Per-host TCP port scan: reached from HostDetailScreen (Tab), not from
// the main menu — a port scan needs a target host, so there's no
// standalone entry point for it (see PlaceholderScreen text for the
// menu's "PORT SCANNER" entry).
class PortScanScreen : public Screen {
public:
    static PortScanScreen& instance();

    void setTarget(const IPAddress& ip) { _target = ip; }

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "PORT"; }
    const char* helpText() const override {
        return "PORT MAPPING\nMENU>NET>HOST>TAB(PORT)\nTCP scan of configured range\n(SETTINGS) + ~50 common ports\nabove 1024 (8080,3306,6379,\nRDP,VNC,...).\nENTER:start/rescan  I:full banner\nArrows:move  DEL:back";
    }

private:
    void drawResults(M5Canvas& gfx, int16_t top);
    void drawTopPortsFooter(M5Canvas& gfx, size_t count);
    bool isForThisHost() const;

    IPAddress _target;
    size_t _selected = 0;
    bool _showDetail = false;
};
