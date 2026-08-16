#pragma once

#include "Screen.h"
#include <Arduino.h>

// "JOIN OPEN NET": reached with 'c' on an OPEN sighting in WAR DRIVING.
// Confirms authorization, joins the open network (no password), then runs
// captive-portal DETECTION and reports the result. It never tries to
// bypass a portal — see net/CaptivePortalDetector.h.
class OpenConnectScreen : public Screen {
public:
    static OpenConnectScreen& instance();

    void setTarget(const String& ssid) { _ssid = ssid; }

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void update(uint32_t nowMs) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "JOIN"; }
    const char* helpText() const override {
        return "JOIN OPEN NET\nMENU>WD>C(JOIN)\nJoins this open network (no\npassword) and checks for a\ncaptive portal - never tries\nto bypass one.\nY: confirm join\nDEL: cancel / back";
    }

private:
    enum class State : uint8_t { Confirm, Connecting, Detecting, Done, Failed };

    static constexpr uint8_t kLogLines = 4;
    static constexpr uint32_t kConnectTimeoutMs = 15000;

    void pushLog(const String& line);

    String _ssid;
    State _state = State::Confirm;
    uint32_t _connectStartMs = 0;
    bool _detectStarted = false;
    String _log[kLogLines];
    uint8_t _logCount = 0;
};
