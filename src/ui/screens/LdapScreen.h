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

    const char* title() const override { return "LDAP"; }
    const char* helpText() const override {
        return "LDAP SWEEP\nMENU>NET>D>Ent(LDAP)\nAnonymous bind + rootDSE read\non port 389 for every host -\nread-only, no real credential.\nENTER: sweep   I: full rootDSE\nArrows: move   DEL: back";
    }

private:
    void drawFindings(M5Canvas& gfx, int16_t top);

    size_t _selected = 0;
    bool _showDetail = false;  // 'I' shows the selected row's full rootDSE text, untruncated
};
