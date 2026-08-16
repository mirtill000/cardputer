#pragma once

#include "Screen.h"

// "RUN ALL": drives DiscoveryRunner, which sequences every discovery tool
// (one-shot UDP/TCP queries then the promiscuous listeners one at a time,
// because they share the radio). Each tool's own screen holds the results;
// this screen just shows the phase + progress while it runs.
class DiscoveryAllScreen : public Screen {
public:
    static DiscoveryAllScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "ALL"; }
    const char* helpText() const override {
        return "RUN ALL DISCOVERY\nMENU>NET>D>Ent(ALL)\nRuns every discovery tool in turn\n(UPnP, mDNS, SNMP, data-store,\nthen CDP/LLDP, passive hosts,\nrogue DHCP - one radio at a time).\n\nENTER: start / stop\nDEL: back (keeps running)";
    }

private:
    static constexpr uint8_t kLogLines = 6;
    void pushLog(const String& line);

    String _log[kLogLines];
    uint8_t _logCount = 0;
};
