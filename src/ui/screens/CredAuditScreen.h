#pragma once

#include "Screen.h"
#include <IPAddress.h>
#include <cstddef>

// "CREDENTIAL GUESS": per-host credential attack, showing a live
// scrolling log of attempts (like a terminal) plus attempt/success
// counters while CredAuditManager runs in the background. Only
// reachable once CredDisclaimerScreen has been accepted this session
// (see HostDetailScreen / CredDisclaimerScreen) — this screen doesn't
// re-check that itself, trusting the single gate upstream.
class CredAuditScreen : public Screen {
public:
    static CredAuditScreen& instance();

    void setTarget(const IPAddress& ip) { _target = ip; }

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "CRED"; }
    const char* helpText() const override {
        return "CREDENTIAL GUESS\nMENU>NET>HOST>C(CRED)\nBrute-forces default/wordlist\ncredentials against this host\n(HTTP/FTP/Telnet).\nENTER: start\nDEL: back (keeps running)";
    }

private:
    static constexpr uint8_t kLogLines = 5;

    IPAddress _target;
    String _log[kLogLines];
    uint8_t _logCount = 0;

    void pushLog(const String& line);
};
