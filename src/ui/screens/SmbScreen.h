#pragma once

#include "Screen.h"
#include <IPAddress.h>

// "SMB POSTURE": two read-only Negotiate exchanges (SMB1 + SMB2) against
// a host's open SMB/NetBIOS port, showing whether SMBv1 is still enabled,
// the modern SMB2 dialect and whether SMB2 signing is required, plus an
// overall Weak/Fair/Ok verdict. Read-only - no login, no share
// enumeration; see scan/SmbNegotiateCheck.h for the scope note.
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
        return "SMB POSTURE\nMENU>NET>HOST>S(SMB)\nSMB1+SMB2 Negotiate - shows\nSMBv1 exposure, dialect,\nsigning + Weak/Fair/Ok\nverdict. No login.\nENTER: start  DEL: back";
    }

private:
    static constexpr uint8_t kLogLines = 5;

    void pushLog(const String& line);

    IPAddress _target;
    uint16_t _port = 445;
    String _log[kLogLines];
    uint8_t _logCount = 0;
};
