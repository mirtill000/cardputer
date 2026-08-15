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
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"

namespace {
// Function-pointer accessors rather than plain Screen* so the singleton
// instances are created on first use, with no static-init-order concerns.
struct DItem {
    const char* label;
    Screen* (*get)();
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

const DItem kItems[] = {
    {"RUN ALL DISCOVERY", gRunAll}, {"LAN TOPOLOGY", gCdp},       {"UPNP DISCOVERY", gSsdp},
    {"SERVICE SCAN", gSvc},         {"PASSIVE HOSTS", gPassive},  {"ROGUE DHCP", gRogue},
    {"SNMP SWEEP", gSnmp},          {"DATASTORE SWEEP", gData},   {"BEACON/PROBE INTEL", gBeaconProbe},
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
            _selected = (_selected == 0) ? (kCount - 1) : (_selected - 1);
            break;
        case UiKey::Down:
            _selected = (_selected + 1) % kCount;
            break;
        case UiKey::Enter:
            g_ui.pushScreen(kItems[_selected].get());
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

    constexpr int16_t kRowH = 11;  // 9 items now (BEACON/PROBE INTEL added) - tighter rows to fit
    constexpr int16_t kTop = 18;
    for (size_t i = 0; i < kCount; i++) {
        int16_t y = kTop + (int16_t)i * kRowH;
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

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("ENTER:open  DEL:back");
}
