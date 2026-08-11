#pragma once

#include "Screen.h"
#include <vector>
#include "../../net/WifiManager.h"

// Scan for nearby WiFi networks, pick one with the keyboard, type its
// password (if any) and connect — no credentials baked into the
// firmware anywhere. On a successful connect, WifiManager persists the
// SSID/password to NVS so the device reconnects on its own next boot.
class WifiSetupScreen : public Screen {
public:
    static WifiSetupScreen& instance();

    void onEnter() override;
    void onExit() override;
    void onKey(UiKey key, char ch) override;
    void update(uint32_t nowMs) override;
    void draw(M5Canvas& gfx) override;

private:
    enum class State { Idle, Scanning, NetworkList, SavedList, PasswordEntry, Connecting, Result };

    void startScan();
    void enterPasswordEntry(const String& ssid);
    void attemptConnect();
    void connectToSavedNetwork(uint8_t index);
    void drawNetworkList(M5Canvas& gfx, int16_t top);
    void drawSavedList(M5Canvas& gfx, int16_t top);

    State _state = State::Idle;
    std::vector<WifiManager::ScanResult> _networks;  // local, deduped-by-SSID+sorted-by-RSSI copy for display
    size_t _selected = 0;
    String _password;
    String _pendingSsid;
    uint32_t _connectStartMs = 0;
    bool _lastConnectOk = false;

    // Which of the two "how did we get to Connecting" paths we're on -
    // decides whether update() should call saveCredentials() (a freshly
    // typed password) or touchSavedNetwork() (reconnecting to an
    // already-saved one, whose stored password must NOT be overwritten
    // with the empty _password this path never fills in).
    bool _savingOnConnect = false;
    int16_t _pendingSavedIndex = -1;
};
