#include "ActivityStatus.h"
#include "Theme.h"
#include "../scan/ArpSpoofManager.h"
#include "../scan/DeauthManager.h"
#include "../scan/PmkidManager.h"
#include "../scan/CdpLldpSniffer.h"
#include "../scan/RogueDhcpDetector.h"
#include "../scan/PassiveHostDiscovery.h"
#include <cstdio>
#include <cstring>

void activity::draw(M5Canvas& gfx, int16_t rightX, int16_t y) {
    // Every promiscuous-mode consumer in the firmware. Short tags keep the
    // header readable when one is active; the count is what matters when
    // more than one is.
    const struct {
        bool on;
        const char* tag;
    } prom[] = {
        {g_arpSpoofManager.isRunning(), "ARP"},
        {g_deauthManager.isRunning(), "DTH"},
        {g_pmkidManager.isRunning(), "PMK"},
        {g_cdpLldpSniffer.isRunning(), "CDP"},
        {g_rogueDhcpDetector.isRunning(), "DHCP"},
        {g_passiveHostDiscovery.isRunning(), "PSV"},
    };

    int count = 0;
    const char* only = nullptr;
    for (const auto& p : prom) {
        if (p.on) {
            count++;
            only = p.tag;
        }
    }

    char buf[10];
    uint16_t color;
    if (count >= 2) {
        snprintf(buf, sizeof(buf), "RF:%d!", count);  // conflict
        color = theme::RED;
    } else if (count == 1) {
        snprintf(buf, sizeof(buf), "RF:%s", only);
        color = theme::AMBER;
    } else {
        return;  // nothing promiscuous running — leave the header clean
    }

    int16_t w = (int16_t)strlen(buf) * theme::GLYPH_W;
    int16_t x = rightX - w;
    if (x < 0) return;
    gfx.setTextColor(color, theme::BG);
    gfx.setCursor(x, y);
    gfx.print(buf);
}
