#pragma once

#include "Screen.h"

// Live RSSI meter for one specific AP, to help physically locate it by
// walking around and watching the bar move — reachable via Tab from a
// selected row in WAR DRIVING's sightings list.
//
// Drives its own continuous WiFi scan loop rather than reading
// WardrivingManager's sightings (whose ~15s cadence is far too slow for
// "walk around and watch it change in real time") - just reuses
// WifiManager's existing beginScan()/scanStatus()/getScanResult() the
// same way WifiSetupScreen's own scan state already does. Like every
// other screen in this app that drives a scan, using this at the same
// time as WAR DRIVING (or WIFI SCAN) means the two compete for the same
// single radio - same accepted limitation as running two scan modules
// together elsewhere, see README.
class SignalFinderScreen : public Screen {
public:
    static SignalFinderScreen& instance();

    void setTarget(const String& ssid, const String& bssid);

    void onEnter() override;
    void update(uint32_t nowMs) override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* helpText() const override {
        return "SIGNAL FINDER\n\nLive RSSI meter for this AP -\nwalk around and watch the bar\nto physically locate it.\nDEL: back";
    }

private:
    String _ssid;
    String _bssid;
    bool _found = false;
    int32_t _rssi = 0;
    int32_t _prevRssi = 0;
    bool _havePrev = false;
    uint32_t _scanCount = 0;
};
