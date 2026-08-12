#pragma once

#include "Screen.h"

// "SERVICE SCAN": drives ServiceEnumerator's DNS-SD browse and lists the
// service instances found (type, instance name, port). Standard mDNS
// discovery — see scan/ServiceEnumerator.h.
class ServiceScreen : public Screen {
public:
    static ServiceScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

private:
    void drawServices(M5Canvas& gfx, int16_t top);

    size_t _selected = 0;
};
