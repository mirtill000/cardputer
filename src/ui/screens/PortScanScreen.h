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

    const char* helpText() const override {
        return "PORT MAPPING\n\nTCP connect-scan of this\nhost's configured port range\n(see SETTINGS).\nENTER: start/rescan  I: full\n  banner\nArrows: move   DEL: back";
    }

private:
    void drawResults(M5Canvas& gfx, int16_t top);
    void drawTopPortsFooter(M5Canvas& gfx, size_t count);
    bool isForThisHost() const;

    IPAddress _target;
    size_t _selected = 0;
    bool _showDetail = false;
};
