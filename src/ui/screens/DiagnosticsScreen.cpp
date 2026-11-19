#include "DiagnosticsScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../Sound.h"
#include "../../net/WifiManager.h"
#include "../../net/TimeSync.h"
#include "../../storage/SdCard.h"
#include <M5Unified.h>
#include <esp_core_dump.h>
#include <esp_system.h>

DiagnosticsScreen& DiagnosticsScreen::instance() {
    static DiagnosticsScreen s;
    return s;
}

void DiagnosticsScreen::onEnter() {
    _lastKey = 0;
    size_t addr = 0, size = 0;
    _coreDumpPresent = esp_core_dump_image_get(&addr, &size) == ESP_OK && size > 0;
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
// Why the chip last reset - the first thing to know after an unexplained
// reboot in the field. "bad" = a crash-type reset (shown amber).
const char* resetReasonLabel(esp_reset_reason_t r, bool& bad) {
    bad = false;
    switch (r) {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_SW: return "restart";
        case ESP_RST_EXT: return "ext-pin";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_PANIC: bad = true; return "PANIC";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT: bad = true; return "WATCHDOG";
        case ESP_RST_BROWNOUT: bad = true; return "BROWNOUT";
        default: return "other";
    }
}

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
    // Uptime + last reset reason, plus "+dump" if a crash coredump is
    // sitting in the coredump partition (dump it over USB with esptool
    // read_flash + espcoredump.py; see partitions.csv).
    bool badReset = false;
    const char* rst = resetReasonLabel(esp_reset_reason(), badReset);
    String upVal = String(ub) + " rst:" + rst + (_coreDumpPresent ? "+dump" : "");
    statusRow(gfx, 80, "UPTIME:", upVal, !badReset && !_coreDumpPresent);

    // Free heap
    statusRow(gfx, 90, "FREE RAM:", String((unsigned)(ESP.getFreeHeap() / 1024)) + " KB", true);

    // Battery-backed RTC (Grove-attached M5Stack RTC Unit) - absent on a
    // bare Cardputer/Cardputer ADV is the expected common case, shown
    // amber same as SD/IMU's "not fitted" state rather than as an error.
    bool rtc = TimeSync::rtcAvailable();
    bool rtcLow = rtc && TimeSync::rtcBatteryLow();
    String rtcVal = !rtc ? "absent" : (rtcLow ? "present, battery LOW" : "present");
    statusRow(gfx, 100, "RTC:", rtcVal, rtc && !rtcLow);

    // B2: smallest free stack this task (the UI/render task) has ever had,
    // in bytes. uxTaskGetStackHighWaterMark(NULL) reports the minimum
    // remaining headroom in words since the task started; multiply by the
    // 4-byte stack word. A number trending toward zero here is the early
    // warning that a task stack size needs raising before it overflows.
    UBaseType_t uiStackFreeWords = uxTaskGetStackHighWaterMark(nullptr);
    statusRow(gfx, 110, "UI STACK:", String((unsigned)(uiStackFreeWords * 4)) + " B free",
              uiStackFreeWords * 4 > 512);

    chrome::drawFooter(gfx, "S:speaker  ?:help  DEL:back");
}
