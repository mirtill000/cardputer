#include "ServiceScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/ServiceEnumerator.h"
#include "../../scan/ScanManager.h"

ServiceScreen& ServiceScreen::instance() {
    static ServiceScreen s;
    return s;
}

void ServiceScreen::onEnter() {
    _selected = 0;
}

void ServiceScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::ServiceEnum) return;  // list itself is pulled live from the enumerator each draw
    if (ev.type != ScanEventType::ScanFinished) return;

    // Correlate this browse's results into the discovery host table (see
    // ScanManager::mergeMdnsService) - same call DiscoveryRunner makes
    // after its own mDNS phase, needed here too since SERVICE SCAN can be
    // run standalone from the DISCOVERY submenu, not just via RUN ALL.
    ServiceEnumerator::Service svc;
    for (size_t i = 0; i < g_serviceEnumerator.count(); i++) {
        if (g_serviceEnumerator.get(i, svc)) {
            g_scanManager.mergeMdnsService(svc.fromIp, svc.type, svc.instance, svc.port);
        }
    }
}

void ServiceScreen::onKey(UiKey key, char ch) {
    if (_showDetail) {
        _showDetail = false;
        return;
    }
    if (key == UiKey::Enter) {
        if (!g_serviceEnumerator.isRunning()) g_serviceEnumerator.start();
    } else if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < g_serviceEnumerator.count()) _selected++;
    } else if (key == UiKey::Char && (ch == 'i' || ch == 'I')) {
        if (g_serviceEnumerator.count() > 0) _showDetail = true;
    } else if (key == UiKey::Back) {
        g_ui.popScreen();
    }
}

void ServiceScreen::draw(M5Canvas& gfx) {
    if (_showDetail) {
        ServiceEnumerator::Service s;
        if (g_serviceEnumerator.get(_selected, s)) {
            bool haveIp = s.fromIp != IPAddress(0, 0, 0, 0);
            String text = "instance: " + s.instance + " / type: " + s.type +
                          (s.port ? (" / port: " + String(s.port)) : String("")) +
                          (haveIp ? (" / from: " + s.fromIp.toString()) : String(""));
            chrome::drawDetailOverlay(gfx, "SERVICE DETAIL", text);
        }
        return;
    }

    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "SERVICE SCAN");

    bool running = g_serviceEnumerator.isRunning();
    gfx.setTextColor(running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("services: ");
    gfx.print((unsigned)g_serviceEnumerator.count());
    gfx.print(running ? "  [browsing...]" : "");

    drawServices(gfx, 30);

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(running ? "querying mDNS..." : "ENTER: browse (DNS-SD)");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print(g_serviceEnumerator.count() > 0 ? "I:full detail  DEL:back" : "DEL:back");
}

void ServiceScreen::drawServices(M5Canvas& gfx, int16_t top) {
    size_t count = g_serviceEnumerator.count();
    if (count == 0) return;

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 8;

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    ServiceEnumerator::Service s;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_serviceEnumerator.get(i, s)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
        gfx.setCursor(6, y);
        String inst = s.instance;
        if (inst.length() > 18) inst = inst.substring(0, 18);
        gfx.print(inst);

        gfx.setTextColor(sel ? theme::CYAN : theme::AMBER, rowBg);
        gfx.setCursor(120, y);
        String type = s.type;
        if (type.length() > 14) type = type.substring(0, 14);
        gfx.print(type);

        if (s.port) {
            gfx.setTextColor(sel ? theme::CYAN : theme::GREY, rowBg);
            gfx.setCursor(210, y);
            gfx.print(s.port);
        }
    }

    chrome::drawScrollMarkers(gfx, top + 2, top + 2 + (int16_t)kMaxRows * kRowH, first > 0, (first + kMaxRows) < count);
}
