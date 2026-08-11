#include "UiManager.h"
#include "Theme.h"
#include "screens/Screen.h"
#include "../core/Config.h"
#include <M5Cardputer.h>

UiManager g_ui;

namespace {
constexpr TickType_t kFrameDelay = pdMS_TO_TICKS(33);  // ~30fps cap on the render loop
constexpr uint32_t kRainTickMs = 70;                   // ~14Hz animation, independent of render fps
constexpr UBaseType_t kScanQueueLen = 24;
}  // namespace

void UiManager::begin() {
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

    _rain.begin(&_canvas, 0, 0, display.width(), display.height(), g_config.rainDensity);

    _scanQueue = xQueueCreate(kScanQueueLen, sizeof(ScanNotification));

    _input.begin();

    xTaskCreatePinnedToCore(&UiManager::taskEntry, "ui", 8192, this, 2, nullptr, 1);
}

void UiManager::activate(Screen* s) {
    _input.setTextEntryMode(false);  // safety net — see setTextEntryMode() in the header
    _canvas.fillScreen(theme::BG);  // wipe whatever the previous screen/rain left behind
    if (s->wantsRain()) {
        _rain.setDensity(s->rainDensity());
        _rain.scatter();
    }
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
    uint32_t lastRainTick = 0;

    for (;;) {
        uint32_t now = millis();

        while (xQueueReceive(_input.queue(), &kev, 0) == pdTRUE) {
            handleKeyEvent(kev);
        }
        while (_scanQueue && xQueueReceive(_scanQueue, &sev, 0) == pdTRUE) {
            if (!_stack.empty()) _stack.back()->onScanEvent(sev);
        }

        Screen* top = _stack.empty() ? nullptr : _stack.back();

        if (top && top->wantsRain() && (now - lastRainTick) >= kRainTickMs) {
            _rain.update();
            lastRainTick = now;
        }

        if (top) {
            top->update(now);
            top->draw(_canvas);
        }
        _canvas.pushSprite(0, 0);

        vTaskDelay(kFrameDelay);
    }
}

void UiManager::handleKeyEvent(const UiKeyEvent& ev) {
    if (!_stack.empty()) _stack.back()->onKey(ev.key, ev.ch);
}
