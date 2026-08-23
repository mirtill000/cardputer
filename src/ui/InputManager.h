#pragma once

#include <atomic>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Abstract UI-level keys, decoupled from the Cardputer's physical key
// layout so screens don't need to know that "up" happens to be the ';'
// key. See InputManager.cpp for the physical mapping.
enum class UiKey : uint8_t {
    None = 0,
    Up,
    Down,
    Left,
    Right,
    Enter,
    Back,
    Tab,
    Char,  // printable ASCII, carried in UiKeyEvent::ch (text-entry fields)
};

struct UiKeyEvent {
    UiKey key = UiKey::None;
    char ch = 0;
};

// Polls the physical keyboard in its own FreeRTOS task and posts
// translated UiKeyEvents to a queue for UiManager to consume. Runs
// independently of the render loop so key handling stays responsive even
// if a frame render takes a while.
class InputManager {
public:
    void begin();
    QueueHandle_t queue() const { return _queue; }

    // When enabled, the ';' '.' ',' '/' keys stop being remapped to
    // Up/Down/Left/Right and come through as plain Char events instead
    // — needed by any screen with a free-text field (e.g. typing a WiFi
    // password), since those characters are common in real passwords.
    // Screens MUST turn this back off when they stop owning the text
    // field (including on early exit/cancel), or every other screen's
    // arrow-key navigation breaks — see UiManager::setTextEntryMode().
    void setTextEntryMode(bool enabled) { _textEntryMode = enabled; }
    bool textEntryMode() const { return _textEntryMode; }

private:
    static void taskEntry(void* arg);
    void run();
    // Translates the current physical keyboard state into a UiKeyEvent;
    // returns false if nothing pressed maps to a UI key. Shared by the
    // initial-press and the auto-repeat paths in run().
    bool decodeKey(UiKeyEvent& ev) const;
    // True only for navigation arrows - the keys that auto-repeat while held.
    static bool isRepeatable(const UiKeyEvent& ev);

    QueueHandle_t _queue = nullptr;
    std::atomic<bool> _textEntryMode{false};

    // Auto-repeat state: while a repeatable (navigation) key stays held
    // with no new change event, re-emit it after kRepeatInitialMs, then
    // every kRepeatIntervalMs - so holding an arrow scrolls instead of
    // needing one press per row. See run().
    UiKeyEvent _repeatEv;
    bool _repeatActive = false;
    uint32_t _nextRepeatMs = 0;
};
