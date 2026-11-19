#include "SsdpScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/SsdpDiscovery.h"

SsdpScreen& SsdpScreen::instance() {
    static SsdpScreen s;
    return s;
}

void SsdpScreen::onEnter() { resetSelection(); }

void SsdpScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // just triggers a redraw via the normal event->draw cycle - no per-line log needed here
}

void SsdpScreen::onListKey(UiKey key, char ch) {
    (void)ch;
    if (key == UiKey::Enter && !g_ssdpDiscovery.isRunning()) g_ssdpDiscovery.start();
}

size_t SsdpScreen::itemCount() const { return g_ssdpDiscovery.deviceCount(); }

const char* SsdpScreen::listFooter() const {
    return g_ssdpDiscovery.deviceCount() > 0 ? "I:full detail  DEL:back" : "DEL:back";
}

void SsdpScreen::drawAboveList(M5Canvas& gfx) {
    bool running = g_ssdpDiscovery.isRunning();
    gfx.setTextColor(running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("found: ");
    gfx.print((unsigned)g_ssdpDiscovery.deviceCount());
    gfx.print(running ? "  [searching...]" : "");

    if (g_ssdpDiscovery.deviceCount() > 0) gfx.drawFastHLine(4, 30, gfx.width() - 8, theme::GREY);

    // Above the footer's accent rule (height()-13).
    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 22);
    gfx.print(running ? "waiting for replies..." : "ENTER: search (M-SEARCH)");
}

void SsdpScreen::drawItem(M5Canvas& gfx, size_t index, int16_t y, int16_t rowH, bool selected) {
    SsdpDiscovery::Device d;
    if (!g_ssdpDiscovery.getDevice(index, d)) return;
    const int16_t lineH = rowH / 2;

    uint16_t rowBg = selected ? theme::PANEL_BG : theme::BG;
    if (selected) gfx.fillRect(0, y, gfx.width(), rowH - 2, rowBg);

    gfx.setTextColor(selected ? theme::CYAN : theme::GREEN, rowBg);
    gfx.setCursor(6, y);
    gfx.print(d.fromIp.toString());
    gfx.setCursor(80, y);
    String server = d.server.length() ? d.server : String("(no SERVER header)");
    if (server.length() > 25) server = server.substring(0, 25);
    gfx.print(server);

    gfx.setTextColor(theme::GREY, rowBg);
    gfx.setCursor(6, y + lineH);
    String usn = d.usn;
    if (usn.length() > 37) usn = usn.substring(0, 37);
    gfx.print(usn);
}

String SsdpScreen::detailText(size_t index) const {
    SsdpDiscovery::Device d;
    if (!g_ssdpDiscovery.getDevice(index, d)) return String();
    _detailTitle = d.fromIp.toString();
    return "SERVER: " + (d.server.length() ? d.server : String("-")) + " / LOCATION: " +
           (d.location.length() ? d.location : String("-")) + " / USN: " + (d.usn.length() ? d.usn : String("-"));
}
