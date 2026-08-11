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
    enum class State { Idle, Scanning, NetworkList, PasswordEntry, Connecting, Result };

    void startScan();
    void enterPasswordEntry(const String& ssid);
    void attemptConnect();
    void drawNetworkList(M5Canvas& gfx, int16_t top);

    State _state = State::Idle;
    std::vector<WifiManager::ScanResult> _networks;  // local, deduped-by-SSID+sorted-by-RSSI copy for display
    size_t _selected = 0;
    String _password;
    String _pendingSsid;
    uint32_t _connectStartMs = 0;
    bool _lastConnectOk = false;
};
