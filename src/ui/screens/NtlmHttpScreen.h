#pragma once

#include "Screen.h"

// "NTLM DISCLOSURE": runs NtlmHttpProbe over every alive host's known
// HTTP port and lists the domain/hostname each NTLM-speaking one leaked
// via its Type 2 challenge. See scan/NtlmHttpProbe.h — read-only
// disclosure check, handshake never completed, no credential involved.
class NtlmHttpScreen : public Screen {
public:
    static NtlmHttpScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

private:
    void drawFindings(M5Canvas& gfx, int16_t top);

    size_t _selected = 0;
};
