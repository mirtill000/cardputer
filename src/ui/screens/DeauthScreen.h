#pragma once

#include "Screen.h"

// "DEAUTH + CAPTURE": single-target handshake capture. setTarget() is
// called (from WardrivingScreen, on a selected sighting) before this
// screen is pushed, pre-filling the AP's BSSID/channel; the user then
// types the ONE client MAC to deauth (never a broadcast/all-clients
// option — see DeauthManager.h). Runs to completion on its own (a
// small fixed burst, then a bounded capture window) — there's no
// "stop early" beyond backing out, since the whole session is already
// time-boxed.
class DeauthScreen : public Screen {
public:
    static DeauthScreen& instance();

    void setTarget(const String& ssid, const String& bssid, uint8_t channel);

    void onEnter() override;
    void onExit() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

private:
    enum class State { EnterClientMac, Running, Done };

    static constexpr uint8_t kLogLines = 5;

    void pushLog(const String& line);

    String _ssid;
    String _bssid;
    uint8_t _channel = 1;
    String _clientMacText;
    State _state = State::EnterClientMac;
    String _log[kLogLines];
    uint8_t _logCount = 0;
};
