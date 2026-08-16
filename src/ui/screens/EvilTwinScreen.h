#pragma once

#include "Screen.h"

// "EVIL TWIN": stands up EvilTwinManager's look-alike open AP. Reachable
// either from MAIN MENU (type an SSID from scratch) or from a selected
// WAR DRIVING sighting via 'E' (pre-fills that network's SSID/channel,
// via setSuggestedSsid()) — either way the SSID stays editable before
// starting.
class EvilTwinScreen : public Screen {
public:
    static EvilTwinScreen& instance();

    void setSuggestedSsid(const String& ssid, uint8_t channel);

    void onEnter() override;
    void onExit() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "TWIN"; }
    const char* helpText() const override {
        return "EVIL TWIN\nMENU>WD>E(TWIN)\nType/edit the SSID, ENTER to\nstart a look-alike open AP.\n(running) ENTER: stop\nDEL: erase / back";
    }

private:
    enum class State { EnterSsid, Running };

    static constexpr uint8_t kLogLines = 5;

    void pushLog(const String& line);

    String _ssidText;
    uint8_t _channel = 1;
    State _state = State::EnterSsid;
    String _log[kLogLines];
    uint8_t _logCount = 0;
};
