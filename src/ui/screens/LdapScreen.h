#pragma once

#include "Screen.h"

// "LDAP SWEEP": runs LdapProbe over the alive-host list and lists every
// LDAP responder found, flagging anonymous bind and showing whatever
// rootDSE attributes it disclosed. See scan/LdapProbe.h — read-only
// detection, no real credentials ever sent.
class LdapScreen : public Screen {
public:
    static LdapScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

private:
    void drawFindings(M5Canvas& gfx, int16_t top);

    size_t _selected = 0;
};
