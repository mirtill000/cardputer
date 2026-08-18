#pragma once

#include "Screen.h"

// "LAN TOPOLOGY": toggles CdpLldpSniffer's passive background listen
// on/off and shows every switch/router neighbor discovered this
// session (device ID + port). Purely passive — see
// scan/CdpLldpSniffer.h for the real limitation on when this actually
// sees anything at all (depends on the AP bridging CDP/LLDP's
// multicast MACs onto the wireless segment).
class CdpLldpScreen : public Screen {
public:
    static CdpLldpScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "LAN"; }
    const char* helpText() const override {
        return "LAN TOPOLOGY\nMENU>NET>D>Ent(LAN)\nPassive CDP/LLDP listen -\nreveals switch/router names\nand ports, no packet sent.\nENTER: start/stop  I: full name\nArrows: move   DEL: back";
    }

private:
    static constexpr uint8_t kLogLines = 4;

    void pushLog(const String& line);
    void drawNeighbors(M5Canvas& gfx, int16_t top);

    bool _running = false;
    String _log[kLogLines];
    uint8_t _logCount = 0;
    size_t _selected = 0;
    bool _showDetail = false;
};
