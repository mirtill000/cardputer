#pragma once

#include "Screen.h"

// "SHARE (QR)": renders a QR code with a compact assessment summary
// (network, host counts, top critical IPs) so it can be pulled onto a
// phone instantly, no SD card handling. Reached with 'Q' from NETWORK
// SCAN. Uses M5GFX's built-in qrcode().
class QrShareScreen : public Screen {
public:
    static QrShareScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "QR"; }
    const char* helpText() const override {
        return "SHARE (QR)\nMENU>NET>Q(QR)\nA QR of the scan summary.\nScan it with a phone to copy\nthe result off the device.\nDEL: back";
    }
};
