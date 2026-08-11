#pragma once

#include <M5GFX.h>
#include <vector>
#include "InputManager.h"
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
    // initialScreen is activated synchronously, on the calling task,
    // BEFORE the render task is created — see the .cpp for why this
    // matters: every pushScreen()/popScreen()/replaceScreen() call in
    // this codebase happens from inside a screen's own onKey(), which
    // only ever runs on the UI task itself (via run() -> handleKeyEvent()),
    // so _stack and _canvas are otherwise never touched from more than
    // one task. Passing the first screen through begin() instead of a
    // separate pushScreen() call after starting the task keeps that
    // invariant true from the very first frame instead of racing setup()
    // (a different task) against the newly-started render task over the
    // same unsynchronized std::vector and M5Canvas.
    void begin(Screen* initialScreen);

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
    InputManager _input;
    std::vector<Screen*> _stack;
    QueueHandle_t _scanQueue = nullptr;

    // Screen timeout: after kIdleTimeoutMs with no key event, the
    // display dims (not blanks - see the .cpp) rather than doing
    // anything to _stack/the active screen. Every background manager
    // (ScanManager, PortScanManager, CredAuditManager, WardrivingManager)
    // is its own independent FreeRTOS task with no dependency on the UI
    // task or on screen state, so none of them notice or care that the
    // display went dim - this is purely a backlight/battery concern.
    uint32_t _lastInputMs = 0;
    bool _dimmed = false;
};

extern UiManager g_ui;
