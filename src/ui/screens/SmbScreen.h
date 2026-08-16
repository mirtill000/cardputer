#pragma once

#include "Screen.h"
#include <IPAddress.h>

// "SMB NEGOTIATE": sends one SMB1 Negotiate Protocol request to a host's
// open SMB/NetBIOS port and shows the Security Mode flags the server
// advertises (user-level vs share-level security, plaintext vs
// challenge/response passwords, SMB signing). Read-only — no login, no
// share enumeration; see scan/SmbNegotiateCheck.h for the scope note.
class SmbScreen : public Screen {
public:
    static SmbScreen& instance();

    void setTarget(const IPAddress& ip, uint16_t port);

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "SMB"; }
    const char* helpText() const override {
        return "SMB NEGOTIATE\nMENU>NET>HOST>S(SMB)\nOne read-only Negotiate\nrequest - shows security mode\nflags, no login attempted.\nENTER: start\nDEL: back";
    }

private:
    static constexpr uint8_t kLogLines = 5;

    void pushLog(const String& line);

    IPAddress _target;
    uint16_t _port = 445;
    String _log[kLogLines];
    uint8_t _logCount = 0;
};
