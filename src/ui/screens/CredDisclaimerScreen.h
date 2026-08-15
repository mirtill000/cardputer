#pragma once

#include "Screen.h"
#include <IPAddress.h>

// Mandatory gate in front of the credential-audit module. Shown once
// per boot the first time the user tries to open a host's credential
// check (see HostDetailScreen) — accepting sets
// AppConfig::credAuditEnabled for the rest of this session so it
// doesn't nag again before every single host, but credAuditEnabled is
// never persisted to NVS (see core/Config.cpp): every fresh boot starts
// back at "not enabled" on purpose.
class CredDisclaimerScreen : public Screen {
public:
    static CredDisclaimerScreen& instance();

    void setPendingTarget(const IPAddress& ip) { _pendingTarget = ip; }

    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* helpText() const override {
        return "AUTHORIZATION REQUIRED\n\nCredential audit attempts real\nlogins against this host.\nY: I'm authorized, proceed\nDEL: cancel";
    }

private:
    IPAddress _pendingTarget;
};
