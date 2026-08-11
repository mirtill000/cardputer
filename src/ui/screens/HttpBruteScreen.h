#pragma once

#include "Screen.h"
#include <IPAddress.h>

// "HTTP PATH BRUTE": dirb-style path enumeration against one already-
// discovered HTTP port, driven by HttpPathBruteforcer.
class HttpBruteScreen : public Screen {
public:
    static HttpBruteScreen& instance();

    void setTarget(const IPAddress& ip, uint16_t port);

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

private:
    static constexpr uint8_t kLogLines = 6;

    void pushLog(const String& line);

    IPAddress _target;
    uint16_t _port = 80;
    String _log[kLogLines];
    uint8_t _logCount = 0;
};
