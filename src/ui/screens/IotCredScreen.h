#pragma once

#include "Screen.h"
#include <cstddef>

// "IOT CREDS": fingerprints discovered hosts (vendor + banners) and tries
// their documented factory-default logins (IotCredScanner). This attempts
// real logins, so it is gated by the same inline consent as SERVICE AUDIT
// (AppConfig::credAuditEnabled) before it will run. Needs a NETWORK SCAN
// (with a port scan) first so there are HTTP/Telnet hosts to check.
class IotCredScreen : public Screen {
public:
    static IotCredScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "IOT"; }
    const char* helpText() const override {
        return "IOT CREDS\n\nTries factory-default logins\non discovered cameras/routers\n(real login attempts, gated).\nNeeds a NETWORK SCAN first.\nENTER: sweep   DEL: back";
    }

private:
    void drawList(M5Canvas& gfx, int16_t top);
    bool _consented = false;
    size_t _selected = 0;
};
