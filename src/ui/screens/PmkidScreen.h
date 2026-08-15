#pragma once

#include "Screen.h"

// "PMKID CAPTURE": drives PmkidManager against one AP (set via
// setTarget() before this screen is pushed, from a selected WAR
// DRIVING sighting) — no client MAC needed, unlike DEAUTH + CAPTURE,
// since this never targets a specific client at all.
class PmkidScreen : public Screen {
public:
    static PmkidScreen& instance();

    void setTarget(const String& ssid, const String& bssid, uint8_t channel);

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* helpText() const override {
        return "PMKID CAPTURE\n\nAssociates with a deliberately\nwrong password to catch a\nPMKID, if this AP offers one.\nNo deauth involved.\nENTER: start\nDEL: back";
    }

private:
    enum class State { Idle, Running, Done };

    static constexpr uint8_t kLogLines = 6;

    void pushLog(const String& line);

    String _ssid;
    String _bssid;
    uint8_t _channel = 1;
    State _state = State::Idle;
    String _log[kLogLines];
    uint8_t _logCount = 0;
};
