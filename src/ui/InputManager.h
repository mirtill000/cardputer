#pragma once

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

private:
    static void taskEntry(void* arg);
    void run();

    QueueHandle_t _queue = nullptr;
};
