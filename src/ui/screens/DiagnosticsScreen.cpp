#include "DiagnosticsScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../Sound.h"
#include "../../net/WifiManager.h"
#include "../../storage/SdCard.h"
#include <M5Unified.h>

DiagnosticsScreen& DiagnosticsScreen::instance() {
    static DiagnosticsScreen s;
    return s;
}

void DiagnosticsScreen::onEnter() {
    _lastKey = 0;
}

void DiagnosticsScreen::onKey(UiKey key, char ch) {
    if (key == UiKey::Back) {
        g_ui.popScreen();
        return;
    }
    if (key == UiKey::Char) {
        _lastKey = ch;
        if (ch == 's' || ch == 'S') M5.Speaker.tone(1500, 150);  // speaker test (bypasses SOUND gate on purpose)
    }
}

namespace {
void statusRow(M5Canvas& gfx, int16_t y, const char* label, const String& value, bool ok) {
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, y);
    gfx.print(label);
    gfx.setTextColor(ok ? theme::GREEN : theme::AMBER, theme::BG);
    gfx.setCursor(6 + 10 * theme::GLYPH_W, y);
    gfx.print(value);
}
}  // namespace

void DiagnosticsScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "DIAGNOSTICS");

    // SD / storage
    statusRow(gfx, 20, "STORAGE:", sdcard::isReady() ? String("SD card ready") : String("LittleFS (no SD)"),
              sdcard::isReady());

    // WiFi
    bool wifi = g_wifi.isConnected();
    statusRow(gfx, 30, "WIFI:", wifi ? (g_wifi.currentSsid() + " " + g_wifi.localIP().toString()) : String("not connected"),
              wifi);

    // Battery
    int32_t batt = M5.Power.getBatteryLevel();
    statusRow(gfx, 40, "BATTERY:", (batt >= 0) ? (String(batt) + "%") : String("no gauge"), batt < 0 || batt >= 20);

    // IMU (BMI270 on the Cardputer ADV)
    bool imu = M5.Imu.isEnabled();
    String imuVal = "absent";
    if (imu) {
        float ax = 0, ay = 0, az = 0;
        M5.Imu.getAccel(&ax, &ay, &az);
        // Avoid printf %f (not always available on this toolchain) - show
        // accel in centi-g as integers.
        char b[28];
        snprintf(b, sizeof(b), "ok %d,%d,%d", (int)(ax * 100), (int)(ay * 100), (int)(az * 100));
        imuVal = b;
    }
    statusRow(gfx, 50, "IMU:", imuVal, imu);

    // Keyboard echo
    String kb = _lastKey ? String("last key '") + _lastKey + "'" : String("press a key...");
    statusRow(gfx, 60, "KEYBOARD:", kb, _lastKey != 0);

    // Speaker
    statusRow(gfx, 70, "SPEAKER:", "press S to test", true);

    // Uptime
    uint32_t up = millis() / 1000;
    char ub[16];
    snprintf(ub, sizeof(ub), "%02u:%02u:%02u", (unsigned)(up / 3600), (unsigned)((up / 60) % 60), (unsigned)(up % 60));
    statusRow(gfx, 80, "UPTIME:", ub, true);

    // Free heap
    statusRow(gfx, 90, "FREE RAM:", String((unsigned)(ESP.getFreeHeap() / 1024)) + " KB", true);

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("S:speaker  ?:help  DEL:back");
}
