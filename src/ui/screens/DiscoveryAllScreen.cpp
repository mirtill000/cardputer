#include "DiscoveryAllScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/DiscoveryRunner.h"

DiscoveryAllScreen& DiscoveryAllScreen::instance() {
    static DiscoveryAllScreen s;
    return s;
}

void DiscoveryAllScreen::onEnter() {
    _logCount = 0;
}

void DiscoveryAllScreen::pushLog(const String& line) {
    if (_logCount < kLogLines) {
        _log[_logCount++] = line;
    } else {
        for (uint8_t i = 1; i < kLogLines; i++) _log[i - 1] = _log[i];
        _log[kLogLines - 1] = line;
    }
}

void DiscoveryAllScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::DiscoveryAll) return;
    if (ev.type == ScanEventType::LogLine) pushLog(String(ev.text));
    else if (ev.type == ScanEventType::ScanStarted) _logCount = 0;
}

void DiscoveryAllScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Enter) {
        if (g_discoveryRunner.isRunning()) {
            g_discoveryRunner.stop();
        } else {
            g_discoveryRunner.start();
        }
        return;
    }
    if (key == UiKey::Back) g_ui.popScreen();  // keeps running in the background
}

namespace {
const char* phaseLabel(DiscoveryRunner::Phase p) {
    switch (p) {
        case DiscoveryRunner::Phase::Upnp: return "UPNP/SSDP";
        case DiscoveryRunner::Phase::Services: return "MDNS SERVICES";
        case DiscoveryRunner::Phase::Snmp: return "SNMP SWEEP";
        case DiscoveryRunner::Phase::DataStore: return "DATASTORE";
        case DiscoveryRunner::Phase::IotOt: return "IOT/OT SWEEP";
        case DiscoveryRunner::Phase::Ldap: return "LDAP SWEEP";
        case DiscoveryRunner::Phase::NtlmHttp: return "NTLM DISCLOSURE";
        case DiscoveryRunner::Phase::LanTopology: return "LAN TOPOLOGY";
        case DiscoveryRunner::Phase::PassiveHosts: return "PASSIVE HOSTS";
        case DiscoveryRunner::Phase::RogueDhcp: return "ROGUE DHCP";
        case DiscoveryRunner::Phase::BeaconProbe: return "BEACON/PROBE";
        case DiscoveryRunner::Phase::Done: return "DONE";
        case DiscoveryRunner::Phase::Failed: return "FAILED";
        default: return "IDLE";
    }
}
}  // namespace

void DiscoveryAllScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "RUN ALL");

    bool running = g_discoveryRunner.isRunning();
    DiscoveryRunner::Phase phase = g_discoveryRunner.phase();

    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("phase: ");
    uint16_t pc = (phase == DiscoveryRunner::Phase::Failed) ? theme::RED
                  : (phase == DiscoveryRunner::Phase::Done) ? theme::GREEN
                                                            : theme::CYAN;
    gfx.setTextColor(pc, theme::BG);
    gfx.print(phaseLabel(phase));

    chrome::drawProgressBar(gfx, 6, 30, gfx.width() - 40, 8, g_discoveryRunner.progressPct());

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, 44, gfx.width() - 8, theme::GREY);

    for (uint8_t i = 0; i < _logCount; i++) {
        int16_t y = 48 + i * 9;
        gfx.setTextColor(theme::AMBER, theme::BG);
        gfx.setCursor(6, y);
        String line = _log[i];
        if (line.length() > 37) line = line.substring(0, 37);
        gfx.print(line);
    }

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(running ? "running (radio shared -> serial)" : "ENTER: run every discovery tool");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print(running ? "ENTER:stop DEL:back(bg) ?:help" : "ENTER:start DEL:back ?:help");
}
