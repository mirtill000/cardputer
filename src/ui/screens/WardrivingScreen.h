#pragma once

#include "Screen.h"

// "WAR DRIVING": toggles WardrivingManager's continuous background AP
// scan on/off, shows a live log + counters while it runs, and manages
// the allowlist that gates its one active behavior (auto-connect +
// discover on an open, allow-listed network) — see
// scan/WardrivingManager.h for the full reasoning on why that's
// allowlist-gated rather than automatic for every open network found.
class WardrivingScreen : public Screen {
public:
    static WardrivingScreen& instance();

    void onEnter() override;
    void onExit() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

private:
    enum class State { Idle, Running, AllowlistView, AllowlistAddEntry, AllowlistAddConfirm };

    static constexpr uint8_t kLogLines = 5;

    void pushLog(const String& line);
    void drawAllowlist(M5Canvas& gfx, int16_t top);
    void drawSightings(M5Canvas& gfx, int16_t top);

    State _state = State::Idle;
    String _log[kLogLines];
    uint8_t _logCount = 0;
    size_t _allowlistSelected = 0;
    String _newSsid;
};
