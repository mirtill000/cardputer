#include "UiManager.h"
#include "Theme.h"
#include "screens/Screen.h"
#include "../core/Config.h"
#include <M5Cardputer.h>

UiManager g_ui;

namespace {
constexpr TickType_t kFrameDelay = pdMS_TO_TICKS(33);  // ~30fps cap on the render loop
constexpr UBaseType_t kScanQueueLen = 24;
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
    _canvas.fillScreen(theme::BG);   // wipe whatever the previous screen left behind
    s->onEnter();
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
            if (!_stack.empty()) _stack.back()->onScanEvent(sev);
        }

        Screen* top = _stack.empty() ? nullptr : _stack.back();
        if (top) {
            top->update(millis());
            top->draw(_canvas);
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
    if (!_stack.empty()) _stack.back()->onKey(ev.key, ev.ch);
}
