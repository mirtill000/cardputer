#pragma once

#include "Screen.h"
#include <IPAddress.h>

// Per-host default-credentials check. Only reachable once
// CredDisclaimerScreen has been accepted this session (see
// HostDetailScreen / CredDisclaimerScreen) — this screen itself doesn't
// re-check that, trusting the single gate upstream.
class CredAuditScreen : public Screen {
public:
    static CredAuditScreen& instance();

    void setTarget(const IPAddress& ip) { _target = ip; }

    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

private:
    IPAddress _target;
};
