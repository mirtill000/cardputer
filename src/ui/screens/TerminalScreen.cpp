#include "TerminalScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../net/WifiManager.h"
#include "../../scan/ScanManager.h"
#include "../../scan/BluetoothManager.h"
#include "../../storage/SdCard.h"
#include <M5Unified.h>
#include <cstdio>

TerminalScreen& TerminalScreen::instance() {
    static TerminalScreen s;
    return s;
}

void TerminalScreen::onEnter() {}

void TerminalScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Back) g_ui.popScreen();
}

namespace {
// A "prompt line" - render "> " in magenta then the actual text in
// green (or a passed color), same terminal-emulator convention.
void promptRow(M5Canvas& gfx, int16_t y, const char* label, const String& value, uint16_t valueColor) {
    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(4, y);
    gfx.print("> ");
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.print(label);
    gfx.setTextColor(valueColor, theme::BG);
    // Position value at a fixed column so the readout aligns like a
    // real terminal table.
    gfx.setCursor(72, y);
    String v = value;
    if (v.length() > 27) v = v.substring(0, 27);
    gfx.print(v);
}
}  // namespace

void TerminalScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "TERMINAL");

    // Uptime in HH:MM:SS.
    uint32_t up = millis() / 1000;
    char ub[16];
    snprintf(ub, sizeof(ub), "%02u:%02u:%02u", (unsigned)(up / 3600),
             (unsigned)((up / 60) % 60), (unsigned)(up % 60));

    // Heap in KB.
    unsigned freeKb = (unsigned)(ESP.getFreeHeap() / 1024);
    unsigned totKb = (unsigned)(ESP.getHeapSize() / 1024);
    char hb[24];
    snprintf(hb, sizeof(hb), "%u/%u KB", freeKb, totKb);

    // WiFi.
    String wifi;
    if (g_wifi.isConnected()) {
        wifi = g_wifi.currentSsid() + " " + g_wifi.localIP().toString();
    } else {
        wifi = "not connected";
    }

    // BLE.
    String ble = g_bluetoothManager.isRunning()
                     ? (String("scanning, ") + String(g_bluetoothManager.deviceCount()) + " dev")
                     : String("idle");

    // Host / port / other network stats.
    char hcb[16];
    snprintf(hcb, sizeof(hcb), "%u hosts", (unsigned)g_scanManager.hostCount());

    // Battery %.
    int32_t batt = M5.Power.getBatteryLevel();
    char bb[8];
    if (batt >= 0) snprintf(bb, sizeof(bb), "%d%%", (int)batt);
    else           snprintf(bb, sizeof(bb), "n/a");

    // Storage.
    String storage = sdcard::isReady() ? String("SD ready") : String("LittleFS only");

    int16_t y = 20;
    constexpr int16_t kStep = 10;
    promptRow(gfx, y, "uptime  ", ub, theme::GREEN);              y += kStep;
    promptRow(gfx, y, "heap    ", hb, theme::CYAN);                y += kStep;
    promptRow(gfx, y, "wifi    ", wifi, g_wifi.isConnected() ? theme::GREEN : theme::AMBER); y += kStep;
    promptRow(gfx, y, "ble     ", ble, g_bluetoothManager.isRunning() ? theme::CYAN : theme::GREY); y += kStep;
    promptRow(gfx, y, "hosts   ", hcb, theme::GREEN);              y += kStep;
    promptRow(gfx, y, "storage ", storage, sdcard::isReady() ? theme::GREEN : theme::AMBER); y += kStep;
    promptRow(gfx, y, "battery ", bb,
              (batt < 0)     ? theme::GREY
              : (batt < 15)  ? theme::RED
              : (batt < 30)  ? theme::AMBER
                             : theme::GREEN);
    y += kStep;

    // Blinking cursor at the "next prompt line" - a small terminal-feel
    // cue that the screen is live, not a static snapshot.
    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(4, y);
    gfx.print("> ");
    if ((millis() / 500) % 2 == 0) {
        gfx.setTextColor(theme::GREEN, theme::BG);
        gfx.print("_");
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back");
}
