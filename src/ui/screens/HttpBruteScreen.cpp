#include "HttpBruteScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/HttpPathBruteforcer.h"

HttpBruteScreen& HttpBruteScreen::instance() {
    static HttpBruteScreen s;
    return s;
}

void HttpBruteScreen::setTarget(const IPAddress& ip, uint16_t port) {
    _target = ip;
    _port = port;
}

void HttpBruteScreen::onEnter() {
    _logCount = 0;
}

void HttpBruteScreen::pushLog(const String& line) {
    if (_logCount < kLogLines) {
        _log[_logCount++] = line;
    } else {
        for (uint8_t i = 1; i < kLogLines; i++) _log[i - 1] = _log[i];
        _log[kLogLines - 1] = line;
    }
}

void HttpBruteScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::HttpBrute) return;
    if (ev.type == ScanEventType::LogLine) pushLog(String(ev.text));
    else if (ev.type == ScanEventType::ScanStarted) _logCount = 0;
}

void HttpBruteScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Enter) {
        if (!g_httpBruteforcer.isRunning()) g_httpBruteforcer.start(_target, _port);
        return;
    }
    if (key == UiKey::Back) g_ui.popScreen();
}

void HttpBruteScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "HTTP PATH BRUTE");

    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("target: ");
    gfx.print(_target.toString());
    gfx.print(":");
    gfx.print(_port);

    bool running = g_httpBruteforcer.isRunning();
    gfx.setTextColor(theme::CYAN, theme::BG);
    gfx.setCursor(6, 28);
    gfx.print("tried:");
    gfx.print((unsigned)g_httpBruteforcer.triedCount());
    gfx.print(" hits:");
    gfx.print((unsigned)g_httpBruteforcer.hitCount());

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, 39, gfx.width() - 8, theme::GREY);

    for (uint8_t i = 0; i < _logCount; i++) {
        int16_t y = 42 + i * 9;
        gfx.setTextColor(theme::AMBER, theme::BG);
        gfx.setCursor(6, y);
        String line = _log[i];
        if (line.length() > 37) line = line.substring(0, 37);
        gfx.print(line);
    }

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(running ? "running..." : "ENTER: start scan");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back (keeps running)");
}
