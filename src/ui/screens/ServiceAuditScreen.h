#pragma once

#include "Screen.h"
#include <IPAddress.h>

// "SERVICE AUDIT": per-host authentication/anonymous-access audit driven
// by ServiceAuditManager. Reached with 'V' from HOST DETAIL. Behind the
// same credential-attack consent as CREDENTIAL AUDIT — if not yet
// acknowledged this session, it shows its own disclaimer first.
class ServiceAuditScreen : public Screen {
public:
    static ServiceAuditScreen& instance();

    void setTarget(const IPAddress& ip) { _target = ip; }

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "AUD"; }
    const char* helpText() const override {
        return "SERVICE AUDIT\nMENU>NET>HOST>V(AUD)\nAnon-access + default-creds\ncheck across this host's\nservices (FTP/SMB/DB/HTTP).\nY: authorize (first time)\nENTER: start   DEL: back";
    }

private:
    void drawFindings(M5Canvas& gfx, int16_t top);
    void beginAudit();

    IPAddress _target;
    bool _consented = false;
    size_t _selected = 0;
};
