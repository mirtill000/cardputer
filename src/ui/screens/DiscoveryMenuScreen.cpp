#include "DiscoveryMenuScreen.h"
#include "DiscoveryAllScreen.h"
#include "CdpLldpScreen.h"
#include "SsdpScreen.h"
#include "ServiceScreen.h"
#include "PassiveHostScreen.h"
#include "RogueDhcpScreen.h"
#include "SnmpScreen.h"
#include "DataStoreScreen.h"
#include "BeaconProbeScreen.h"
#include "LdapScreen.h"
#include "NtlmHttpScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"

namespace {
// Function-pointer accessors rather than plain Screen* so the singleton
// instances are created on first use, with no static-init-order concerns.
struct DItem {
    const char* label;
    Screen* (*get)();  // nullptr marks a section-header row: not selectable, opens nothing
};
Screen* gRunAll() { return &DiscoveryAllScreen::instance(); }
Screen* gCdp() { return &CdpLldpScreen::instance(); }
Screen* gSsdp() { return &SsdpScreen::instance(); }
Screen* gSvc() { return &ServiceScreen::instance(); }
Screen* gPassive() { return &PassiveHostScreen::instance(); }
Screen* gRogue() { return &RogueDhcpScreen::instance(); }
Screen* gSnmp() { return &SnmpScreen::instance(); }
Screen* gData() { return &DataStoreScreen::instance(); }
Screen* gBeaconProbe() { return &BeaconProbeScreen::instance(); }
Screen* gLdap() { return &LdapScreen::instance(); }
Screen* gNtlmHttp() { return &NtlmHttpScreen::instance(); }

// Grouped by prerequisite so the now-11-tool list reads as sections
// instead of one flat run: standalone "run everything" first, then
// one-shot active tools, tools that want a NETWORK SCAN's host table
// first, the one tool that also wants a PORT SCAN, and finally the
// passive listeners that just sit and collect.
const DItem kItems[] = {
    {"RUN ALL DISCOVERY", gRunAll},
    {"-- ONE-SHOT --", nullptr},
    {"UPNP DISCOVERY", gSsdp},
    {"SERVICE SCAN", gSvc},
    {"-- NEEDS NETWORK SCAN --", nullptr},
    {"SNMP SWEEP", gSnmp},
    {"DATASTORE SWEEP", gData},
    {"LDAP SWEEP", gLdap},
    {"-- NEEDS PORT SCAN --", nullptr},
    {"NTLM DISCLOSURE", gNtlmHttp},
    {"-- PASSIVE LISTENERS --", nullptr},
    {"LAN TOPOLOGY", gCdp},
    {"PASSIVE HOSTS", gPassive},
    {"ROGUE DHCP", gRogue},
    {"BEACON/PROBE INTEL", gBeaconProbe},
};
constexpr size_t kCount = sizeof(kItems) / sizeof(kItems[0]);
}  // namespace

DiscoveryMenuScreen& DiscoveryMenuScreen::instance() {
    static DiscoveryMenuScreen s;
    return s;
}

void DiscoveryMenuScreen::onEnter() {
    if (_selected >= kCount) _selected = 0;
}

void DiscoveryMenuScreen::onKey(UiKey key, char /*ch*/) {
    switch (key) {
        case UiKey::Up:
            // Section-header rows (get == nullptr) aren't selectable -
            // step past them to the next real item.
            do {
                _selected = (_selected == 0) ? (kCount - 1) : (_selected - 1);
            } while (kItems[_selected].get == nullptr);
            break;
        case UiKey::Down:
            do {
                _selected = (_selected + 1) % kCount;
            } while (kItems[_selected].get == nullptr);
            break;
        case UiKey::Enter:
            if (kItems[_selected].get) g_ui.pushScreen(kItems[_selected].get());
            break;
        case UiKey::Back:
            g_ui.popScreen();
            break;
        default:
            break;
    }
}

void DiscoveryMenuScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "DISCOVERY");

    // 11 tools plus 4 section-header rows now - too many to keep cramming
    // into ever-shorter rows on a 135px-tall screen the way this list did
    // up through 9 items. Scrolls instead, same first/kMaxRows windowing
    // every findings list in this firmware already uses (CdpLldpScreen,
    // ServiceScreen, DataStoreScreen, ...).
    constexpr int16_t kRowH = 13;
    constexpr int16_t kTop = 18;
    constexpr size_t kMaxRows = 8;

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= kCount) break;
        int16_t y = kTop + (int16_t)row * kRowH;

        if (!kItems[i].get) {
            // Section header: dim label, no selection box - just a
            // separator between groups of tools.
            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(8, y + 2);
            gfx.print(kItems[i].label);
            continue;
        }

        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        gfx.drawRect(4, y, gfx.width() - 8, kRowH - 2, sel ? theme::CYAN : theme::GREY);
        gfx.fillRect(5, y + 1, gfx.width() - 10, kRowH - 4, rowBg);

        gfx.setTextColor(sel ? theme::MAGENTA : theme::GREY, rowBg);
        gfx.setCursor(10, y + 2);
        gfx.print(sel ? "> " : "  ");
        gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
        gfx.print(kItems[i].label);
    }

    chrome::drawScrollMarkers(gfx, kTop, kTop + (int16_t)kMaxRows * kRowH, first > 0, (first + kMaxRows) < kCount);

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("ENTER:open  DEL:back");
}
