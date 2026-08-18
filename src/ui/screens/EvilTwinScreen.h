#pragma once

#include "Screen.h"

// "EVIL TWIN": stands up EvilTwinManager's look-alike open AP. Reachable
// from a selected WAR DRIVING sighting via 'E' (pre-fills that
// network's SSID/channel, via setSuggestedSsid()) — the SSID stays
// editable before starting. TAB starts KARMA mode instead: cycles the
// AP through SSIDs nearby clients have already been overheard probing
// for (see EvilTwinManager.h), no typing needed - the typed SSID field
// is ignored for that path. TAB rather than a letter key because text
// entry is active here (typing the SSID) and a letter needs to stay
// available as a literal character.
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
        return "EVIL TWIN\nMENU>WD>E(TWIN)\nType SSID, ENTER: fixed AP.\nTAB: KARMA (cycles probed\nSSIDs, no typing needed).\n(running) ENTER: stop\nDEL: erase / back";
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
