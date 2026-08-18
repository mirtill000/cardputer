#pragma once

#include "Screen.h"

// "BEACON/PROBE INTEL": toggles BeaconProbeSniffer's channel-hopping
// passive listen on/off and shows two views - reachable devices' beacons/
// probe-responses (AP intel, incl. hidden-SSID reveals) and nearby
// clients' probe requests (their probed-SSID PNL) - switched with TAB.
// See scan/BeaconProbeSniffer.h for what this does and doesn't do.
class BeaconProbeScreen : public Screen {
public:
    static BeaconProbeScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "BCN"; }
    const char* helpText() const override {
        return "BEACON/PROBE INTEL\nMENU>NET>D>Ent(BCN)\nTAB:switch view  ENTER:start/stop\nI:detail  Arrows:move  DEL:back\nW on AP row=WPS (red=unlocked,\namber=locked). Detect only.\nHops channels - your WiFi drops\nwhile on, reconnects on stop.";
    }

private:
    enum class View { Aps, Clients };

    static constexpr uint8_t kLogLines = 2;

    void pushLog(const String& line);
    void drawAps(M5Canvas& gfx, int16_t top);
    void drawClients(M5Canvas& gfx, int16_t top);

    View _view = View::Aps;
    bool _running = false;
    String _log[kLogLines];
    uint8_t _logCount = 0;
    size_t _apSelected = 0;
    size_t _clientSelected = 0;
    bool _showDetail = false;
};
