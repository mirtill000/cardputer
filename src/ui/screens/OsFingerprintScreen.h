#pragma once

#include "Screen.h"

// "OS FINGERPRINT": toggles OsFingerprint's background listen and lists
// every host seen answering a TCP handshake (SYN-ACK), with the TTL
// bucket, raw window size and TCP option order observed. See
// scan/OsFingerprint.h for exactly what's a real claim here (TTL
// bucket) versus raw data shown for a human to interpret (window,
// option order) rather than a fitted OS/version guess.
class OsFingerprintScreen : public Screen {
public:
    static OsFingerprintScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "OSFP"; }
    const char* helpText() const override {
        return "OS FINGERPRINT\nMENU>NET>D>Ent(OSFP)\nReads TTL/window/TCP options\nfrom SYN-ACK replies seen -\nno probes sent. I:detail\nArrows:move  ENTER:start/stop\nDEL:back (open networks only)";
    }

private:
    void drawHosts(M5Canvas& gfx, int16_t top);

    bool _running = false;
    size_t _selected = 0;
    bool _showDetail = false;
};
