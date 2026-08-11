#pragma once

#include "Screen.h"

// "BLE SCAN": Bluetooth Low Energy sibling of WAR DRIVING - toggles
// BleScanManager's continuous background passive BLE survey on/off and
// shows a live log plus a list of every device seen this session
// (address, name if advertised, RSSI). Purely passive, same as
// WardrivingManager's default (non-allowlisted) behavior - never
// pairs/connects to anything, so there's no allowlist/confirmation flow
// here at all. See scan/BleScanManager.h for the full reasoning.
class BleScanScreen : public Screen {
public:
    static BleScanScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

private:
    enum class State { Idle, Running };

    static constexpr uint8_t kLogLines = 5;

    void pushLog(const String& line);
    void drawSightings(M5Canvas& gfx, int16_t top);

    State _state = State::Idle;
    String _log[kLogLines];
    uint8_t _logCount = 0;
    size_t _sightingsSelected = 0;
};
