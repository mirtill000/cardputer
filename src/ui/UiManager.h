#pragma once

#include <M5GFX.h>
#include <vector>
#include "InputManager.h"
#include "MatrixRain.h"
#include "../core/EventQueue.h"

class Screen;

// Owns the render loop: a single full-screen M5Canvas used as an
// off-screen compositing buffer (drawn into, then pushed to the LCD in
// one SPI burst — avoids tearing without needing a second framebuffer),
// the screen navigation stack, and the queues that feed it input events
// and scan-result notifications.
//
// Runs as its own FreeRTOS task on core 1 alongside InputManager, kept
// separate from the ESP32 Arduino loopTask (which we leave essentially
// idle — see main.cpp) so a slow render frame can never delay keyboard
// polling or vice versa.
class UiManager {
public:
    void begin();

    void pushScreen(Screen* s);
    void popScreen();
    void replaceScreen(Screen* s);

    // Scan modules post ScanNotification here; the UI task drains it and
    // forwards each notification to the active screen's onScanEvent().
    QueueHandle_t scanQueue() const { return _scanQueue; }

    // Forwards to InputManager — see its header for what this does and
    // why a screen must turn it back off. Also force-reset to off on
    // every screen transition (push/pop/replace) as a safety net, so a
    // screen that forgets to clean up on exit can't permanently wedge
    // arrow-key navigation everywhere else in the app.
    void setTextEntryMode(bool enabled) { _input.setTextEntryMode(enabled); }

private:
    static void taskEntry(void* arg);
    void run();
    void handleKeyEvent(const UiKeyEvent& ev);
    void activate(Screen* s);

    M5Canvas _canvas;
    MatrixRain _rain;
    InputManager _input;
    std::vector<Screen*> _stack;
    QueueHandle_t _scanQueue = nullptr;
};

extern UiManager g_ui;
