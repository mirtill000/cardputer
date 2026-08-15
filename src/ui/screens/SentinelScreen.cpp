#include "SentinelScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/SentinelManager.h"
#include "../../net/WifiManager.h"

namespace {
const char* eventLabel(SentinelManager::EventKind k) {
    switch (k) {
        case SentinelManager::EventKind::NewDevice: return "NEW";
        case SentinelManager::EventKind::DeviceGone: return "GONE";
        case SentinelManager::EventKind::DeauthFlood: return "FLOOD";
        default: return "?";
    }
}

uint16_t eventColor(SentinelManager::EventKind k, bool selected) {
    if (selected) return theme::CYAN;
    switch (k) {
        case SentinelManager::EventKind::NewDevice: return theme::MAGENTA;
        case SentinelManager::EventKind::DeviceGone: return theme::AMBER;
        case SentinelManager::EventKind::DeauthFlood: return theme::RED;
        default: return theme::GREY;
    }
}
}  // namespace

SentinelScreen& SentinelScreen::instance() {
    static SentinelScreen s;
    return s;
}

void SentinelScreen::onEnter() {
    _selected = 0;
}

void SentinelScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // the list is pulled live from the manager on each draw - no per-line log kept here
}

void SentinelScreen::onKey(UiKey key, char ch) {
    if (_showDetail) {
        _showDetail = false;
        return;
    }
    if (key == UiKey::Char && (ch == 'i' || ch == 'I')) {
        if (g_sentinelManager.eventLogCount() > 0) _showDetail = true;
        return;
    }
    switch (key) {
        case UiKey::Enter:
            if (g_sentinelManager.isRunning()) {
                g_sentinelManager.stop();
            } else {
                g_sentinelManager.start();  // silently no-ops without WiFi - see the status line
            }
            break;
        case UiKey::Up:
            if (_selected > 0) _selected--;
            break;
        case UiKey::Down:
            if (_selected + 1 < g_sentinelManager.eventLogCount()) _selected++;
            break;
        case UiKey::Back:
            g_ui.popScreen();  // keeps running in the background - see SentinelManager
            break;
        default:
            break;
    }
}

void SentinelScreen::draw(M5Canvas& gfx) {
    if (_showDetail) {
        SentinelManager::Event ev;
        if (g_sentinelManager.getEvent(_selected, ev)) {
            String text;
            if (ev.kind == SentinelManager::EventKind::DeauthFlood) {
                text = String("deauth/disassoc flood against BSSID ") + ev.mac;
            } else {
                text = String(ev.kind == SentinelManager::EventKind::NewDevice ? "new device" : "device gone") +
                       " / IP: " + ev.ip.toString() + " / MAC: " + ev.mac + " / host: " +
                       (ev.hostname.length() ? ev.hostname : String("?")) + " / vendor: " +
                       (ev.vendor.length() ? ev.vendor : String("unknown"));
            }
            chrome::drawDetailOverlay(gfx, eventLabel(ev.kind), text);
        }
        return;
    }

    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "SENTINEL MODE");

    bool running = g_sentinelManager.isRunning();
    bool connected = g_wifi.isConnected();

    gfx.setTextColor(running ? theme::CYAN : (connected ? theme::GREEN : theme::AMBER), theme::BG);
    gfx.setCursor(6, 18);
    if (running) {
        gfx.print("net: ");
        String net = g_sentinelManager.network();
        if (net.length() > 24) net = net.substring(0, 24);
        gfx.print(net);
    } else if (!connected) {
        gfx.print("connect to WiFi first");
    } else {
        gfx.print("not watching");
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, 28);
    gfx.print("new: ");
    gfx.print((unsigned)g_sentinelManager.newDeviceCount());
    gfx.print("  gone: ");
    gfx.print((unsigned)g_sentinelManager.goneDeviceCount());
    gfx.print("  flood: ");
    gfx.print((unsigned)g_sentinelManager.floodCount());

    gfx.setCursor(6, 38);
    gfx.print("frames: ");
    gfx.print((unsigned)g_sentinelManager.capturedFrames());
    gfx.print("  parts: ");
    gfx.print((unsigned)g_sentinelManager.pcapPartCount());
    gfx.print("  cycles: ");
    gfx.print((unsigned)g_sentinelManager.cyclesRun());

    drawEvents(gfx, 48);

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(running ? "ENTER: stop" : "ENTER: start (needs WiFi)");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print(g_sentinelManager.eventLogCount() > 0 ? "I:detail  DEL:back" : "DEL:back");
}

void SentinelScreen::drawEvents(M5Canvas& gfx, int16_t top) {
    size_t count = g_sentinelManager.eventLogCount();
    if (count == 0) {
        chrome::drawEmptyState(gfx, "no events yet",
                                g_sentinelManager.isRunning() ? "watching..." : "press ENTER to start");
        return;
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 6;
    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    SentinelManager::Event ev;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_sentinelManager.getEvent(i, ev)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(eventColor(ev.kind, sel), rowBg);
        gfx.setCursor(6, y);
        gfx.print(eventLabel(ev.kind));

        gfx.setTextColor(sel ? theme::CYAN : theme::GREY, rowBg);
        gfx.setCursor(50, y);
        if (ev.kind == SentinelManager::EventKind::DeauthFlood) {
            gfx.print(ev.mac);  // BSSID, for this event kind
        } else {
            String label = ev.hostname.length() ? ev.hostname : ev.ip.toString();
            if (label.length() > 22) label = label.substring(0, 22);
            gfx.print(label);
        }
    }

    chrome::drawScrollMarkers(gfx, top + 2, top + 2 + (int16_t)kMaxRows * kRowH, first > 0, (first + kMaxRows) < count);
}
