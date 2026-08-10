#include "InputManager.h"
#include <M5Cardputer.h>

namespace {
constexpr UBaseType_t kQueueLen = 16;
constexpr TickType_t kPollDelay = pdMS_TO_TICKS(20);  // ~50Hz poll, well under human reaction time
}  // namespace

void InputManager::begin() {
    _queue = xQueueCreate(kQueueLen, sizeof(UiKeyEvent));
    xTaskCreatePinnedToCore(&InputManager::taskEntry, "input", 3072, this, 3, nullptr, 1);
}

void InputManager::taskEntry(void* arg) {
    static_cast<InputManager*>(arg)->run();
}

void InputManager::run() {
    for (;;) {
        M5Cardputer.update();

        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            auto status = M5Cardputer.Keyboard.keysState();
            UiKeyEvent ev;

            // The Cardputer has no dedicated arrow keys; the small
            // inverted-T cluster on the bottom row (',' ';' '.' '/')
            // doubles as Left/Up/Down/Right in most M5Stack examples and
            // games, so we follow that convention here.
            if (status.enter) {
                ev.key = UiKey::Enter;
            } else if (status.del) {
                ev.key = UiKey::Back;
            } else if (status.tab) {
                ev.key = UiKey::Tab;
            } else {
                for (char c : status.word) {
                    switch (c) {
                        case ';': ev.key = UiKey::Up; break;
                        case '.': ev.key = UiKey::Down; break;
                        case ',': ev.key = UiKey::Left; break;
                        case '/': ev.key = UiKey::Right; break;
                        default:  ev.key = UiKey::Char; ev.ch = c; break;
                    }
                    break;  // one logical key per event; simultaneous chars are rare on this keyboard
                }
            }

            if (ev.key != UiKey::None) {
                xQueueSend(_queue, &ev, 0);
            }
        }

        vTaskDelay(kPollDelay);
    }
}
