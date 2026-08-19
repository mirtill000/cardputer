#include "UiManager.h"
#include "Theme.h"
#include "Chrome.h"
#include "Sound.h"
#include "screens/Screen.h"
#include "../core/Config.h"
#include "../net/TimeSync.h"
#include <M5Cardputer.h>
#include <cstring>

UiManager g_ui;

namespace {
constexpr TickType_t kFrameDelay = pdMS_TO_TICKS(33);  // ~30fps cap on the render loop
constexpr UBaseType_t kScanQueueLen = 24;

// Screen timeout: dim the backlight after this long with no key event,
// restore full brightness on the next one. Deliberately dims rather
// than blanking the canvas or touching _stack - the active screen keeps
// drawing exactly as it would otherwise (so nothing has to change about
// how any Screen subclass works), only the physical backlight level
// changes. 12/255 is dim but not zero: a scan/war-driving session left
// running unattended is still glanceable, not fully dark.
constexpr uint32_t kIdleTimeoutMs = 30000;
constexpr uint32_t kLowPowerTimeoutMs = 8000;  // faster dim when lowPowerMode is on
constexpr uint8_t kDimBrightness = 12;
constexpr uint8_t kFullBrightness = 255;

// Battery alert thresholds (see run()): a one-shot beep + red header at
// the low mark, then a repeated beep + persistent on-screen banner once
// it drops to critical, so a near-dead device can't be slept through.
constexpr int kLowBattPct = 15;
constexpr int kCriticalBattPct = 7;
}  // namespace

void UiManager::begin(Screen* initialScreen) {
    auto& display = M5Cardputer.Display;

    // No PSRAM on this board (ESP32-S3FN8) — say so explicitly rather
    // than relying on LovyanGFX's automatic fallback, so a future port
    // to a PSRAM-equipped board doesn't silently start allocating this
    // 240x135x2-byte (~65KB) buffer out of scarce internal SRAM by
    // accident when PSRAM was actually available and preferable.
    _canvas.setPsram(false);
    if (!_canvas.createSprite(display.width(), display.height())) {
        // Extremely unlikely at ~65KB with no PSRAM, but if the heap is
        // ever fragmented enough (e.g. by WiFi/TLS buffers during a
        // scan) to fail this, we want it in the log instead of a silent
        // black screen.
        log_e("UiManager: failed to allocate render canvas");
    }
    _canvas.setTextFont(1);
    _canvas.setTextSize(1);
    _canvas.setTextWrap(false);
    _canvas.fillScreen(theme::BG);

    _scanQueue = xQueueCreate(kScanQueueLen, sizeof(ScanNotification));

    _input.begin();
    _lastInputMs = millis();

    // Push+activate the first screen here, synchronously, still on
    // whichever task called begin() (normally the Arduino setup() /
    // loopTask) — and only THEN start the render task. Doing it in this
    // order means the render task's first loop iteration already finds
    // a non-empty, fully-settled _stack, instead of a window where
    // setup() and the freshly-started render task could both be
    // touching _stack/_canvas at once. See the ordering note on
    // begin()'s declaration in the header.
    if (initialScreen) {
        _stack.push_back(initialScreen);
        activate(initialScreen);
    }

    xTaskCreatePinnedToCore(&UiManager::taskEntry, "ui", 8192, this, 2, nullptr, 1);
}

void UiManager::activate(Screen* s) {
    _input.setTextEntryMode(false);  // safety net — see setTextEntryMode() in the header
    _helpVisible = false;            // don't carry a help overlay across screens
    _canvas.fillScreen(theme::BG);   // wipe whatever the previous screen left behind
    s->onEnter();
}

String UiManager::breadcrumbPath() const {
    String path;
    for (size_t i = 0; i + 1 < _stack.size(); i++) {  // every ancestor, excluding the active (top) screen
        const char* t = _stack[i]->title();
        if (!t || !t[0]) continue;
        if (path.length()) path += "/";
        path += t;
    }
    return path;
}

void UiManager::pushScreen(Screen* s) {
    if (!_stack.empty()) _stack.back()->onExit();
    _stack.push_back(s);
    activate(s);
}

void UiManager::popScreen() {
    if (_stack.empty()) return;
    _stack.back()->onExit();
    _stack.pop_back();
    if (!_stack.empty()) activate(_stack.back());
}

void UiManager::replaceScreen(Screen* s) {
    if (!_stack.empty()) {
        _stack.back()->onExit();
        _stack.pop_back();
    }
    _stack.push_back(s);
    activate(s);
}

void UiManager::taskEntry(void* arg) {
    static_cast<UiManager*>(arg)->run();
}

void UiManager::run() {
    UiKeyEvent kev;
    ScanNotification sev;

    for (;;) {
        while (xQueueReceive(_input.queue(), &kev, 0) == pdTRUE) {
            handleKeyEvent(kev);
        }
        while (_scanQueue && xQueueReceive(_scanQueue, &sev, 0) == pdTRUE) {
            // Completion feedback: a short non-blocking blip whenever any
            // background scan finishes, so the user doesn't have to stare
            // at the screen to know it's done (respects SOUND setting).
            if (sev.type == ScanEventType::ScanFinished) sound::playDone();
            // Wake the backlight for events a user may have stepped away
            // to catch: any scan finishing, plus any notification from the
            // standing background monitors (SENTINEL / WAR DRIVING / EVIL
            // TWIN), whose log lines ARE the alerts (new device, evil
            // twin, deauth flood, ...). Routine foreground-scan progress
            // deliberately does NOT wake it, so a long attended scan still
            // dims as before.
            if (sev.type == ScanEventType::ScanFinished || sev.source == ScanSource::Sentinel ||
                sev.source == ScanSource::Wardriving || sev.source == ScanSource::EvilTwin) {
                _lastInputMs = millis();
            }
            if (!_stack.empty()) _stack.back()->onScanEvent(sev);
        }

        Screen* top = _stack.empty() ? nullptr : _stack.back();
        if (top) {
            top->update(millis());
            top->draw(_canvas);
            if (_helpVisible) drawHelpOverlay(top);
        }

        // Low-battery alerts (checked every ~5s, not per frame). A
        // one-shot beep with hysteresis at kLowBattPct; below
        // kCriticalBattPct it re-beeps every check and the per-frame
        // banner (further down) stays up until charging lifts it. Header
        // shows the level in red regardless (see chrome::drawHeader).
        if (millis() - _lastBattCheckMs > 5000) {
            _lastBattCheckMs = millis();
            int32_t b = M5.Power.getBatteryLevel();
            _lastBattLevel = (b > 100) ? 100 : (int)b;  // -1 stays -1 (no battery/unknown)
            if (b >= 0 && b < kLowBattPct) {
                bool crossedIn = !_lowBattWarned;
                // Critical band re-beeps every check so it can't be slept
                // through; the kCriticalBattPct..kLowBattPct band beeps
                // once (hysteresis via _lowBattWarned, re-armed above 20%).
                if (b < kCriticalBattPct || crossedIn) sound::playAlert();
                if (crossedIn) {
                    _lowBattWarned = true;
                    _lastInputMs = millis();  // wake the screen so the warning is actually seen
                }
            } else if (b >= 20) {
                _lowBattWarned = false;
            }

            // Same 5s cadence as the battery check above (not per-frame -
            // no reason to touch M5Unified's RTC state 30x/sec): writes a
            // fresh NTP-corrected time back to a battery-backed RTC, if
            // one's attached, once a grace period after sync and then
            // periodically - see TimeSync::syncRtcIfNeeded()'s own comment
            // for why it isn't just "on every sync".
            TimeSync::syncRtcIfNeeded();
        }

        // Idle timeout dims the backlight - see kIdleTimeoutMs above.
        // Toggled only on the edge (not every frame) since
        // setBrightness() talks to the display over SPI and there's no
        // reason to repeat that 30x/sec while nothing has changed.
        uint32_t idleTimeout = g_config.lowPowerMode ? kLowPowerTimeoutMs : kIdleTimeoutMs;
        bool idle = (millis() - _lastInputMs) > idleTimeout;
        if (idle != _dimmed) {
            M5Cardputer.Display.setBrightness(idle ? kDimBrightness : kFullBrightness);
            _dimmed = idle;
        }

        // Persistent critical-battery banner, drawn over the footer so
        // it's impossible to miss unlike the one-shot beep. Uses the
        // level sampled on the ~5s cadence above, not a fresh per-frame
        // read.
        if (_lastBattLevel >= 0 && _lastBattLevel < kCriticalBattPct) {
            int16_t by = _canvas.height() - 9;
            _canvas.fillRect(0, by - 1, _canvas.width(), 10, theme::RED);
            _canvas.setTextColor(theme::BG, theme::RED);
            _canvas.setCursor(4, by);
            _canvas.print(String("! BATTERY ") + String(_lastBattLevel) + "% - CHARGE NOW");
        }
        // Explicit destination, not the 2-arg pushSprite(x,y): that
        // overload pushes to a "parent" LovyanGFX* the sprite has to
        // have been told about, and _canvas (a bare member, default-
        // constructed with no parent) never was — which is exactly
        // what crashed on real hardware (LoadProhibited inside
        // push_sprite(), null parent pointer). Passing the display
        // explicitly here sidesteps needing that stored parent at all.
        _canvas.pushSprite(&M5Cardputer.Display, 0, 0);

        vTaskDelay(kFrameDelay);
    }
}

void UiManager::handleKeyEvent(const UiKeyEvent& ev) {
    _lastInputMs = millis();

    // Global '?' help toggle, intercepted before the screen sees it — but
    // NOT while a text field owns the keyboard (there '?' is a literal
    // character the field needs). Any key while the overlay is up closes it.
    if (_helpVisible) {
        _helpVisible = false;
        return;
    }
    if (ev.key == UiKey::Char && ev.ch == '?' && !_input.textEntryMode()) {
        _helpVisible = true;
        return;
    }

    if (!_stack.empty()) _stack.back()->onKey(ev.key, ev.ch);
}

void UiManager::drawHelpOverlay(Screen* top) {
    // Dim the screen, then a bordered panel with the active screen's
    // help lines (or a generic hint if it doesn't provide any).
    _canvas.fillRect(0, 0, _canvas.width(), _canvas.height(), theme::BG);
    _canvas.drawRect(6, 6, _canvas.width() - 12, _canvas.height() - 12, theme::CYAN);

    _canvas.setTextColor(theme::MAGENTA, theme::BG);
    _canvas.setCursor(12, 12);
    _canvas.print(">> HELP");

    const char* help = top->helpText();
    int16_t y = 26;
    if (help && help[0]) {
        // Render '\n'-separated lines. Bounded to leave room for the
        // global-conventions line below, which every screen's overlay
        // shows regardless of what it says itself.
        const char* p = help;
        char line[42];
        while (*p && y < _canvas.height() - 32) {
            size_t n = 0;
            while (*p && *p != '\n' && n < sizeof(line) - 1) line[n++] = *p++;
            line[n] = '\0';
            if (*p == '\n') p++;
            _canvas.setTextColor(theme::GREEN, theme::BG);
            _canvas.setCursor(12, y);
            _canvas.print(line);
            y += 10;
        }
    } else {
        _canvas.setTextColor(theme::GREY, theme::BG);
        _canvas.setCursor(12, y);
        _canvas.print("No screen-specific help.");
        y += 12;
        _canvas.setTextColor(theme::GREEN, theme::BG);
        _canvas.setCursor(12, y);
        _canvas.print("Arrows: move   ENTER: select");
        y += 10;
        _canvas.setCursor(12, y);
        _canvas.print("DEL: back");
    }

    // Universal-conventions reminder, shown on every screen's overlay
    // regardless of its own helpText() above - 'I'/TAB aren't bound
    // everywhere (each screen's own help lines above say whether they
    // apply here), but this is where a user who's forgotten what they
    // mean project-wide can always find the answer.
    _canvas.setTextColor(theme::AMBER, theme::BG);
    _canvas.setCursor(12, _canvas.height() - 26);
    _canvas.print("I/TAB vary by screen  ?:this help");

    _canvas.setTextColor(theme::GREY, theme::BG);
    _canvas.setCursor(12, _canvas.height() - 16);
    _canvas.print("any key: close");
}
