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
                // Space isn't special-cased here: unlike enter/del/tab
                // (dedicated boolean flags on KeysState because they
                // have no printable ASCII form of their own), the space
                // bar is a normal character key and comes through as
                // ' ' in status.word like any other, so the loop below
                // already handles it via the UiKey::Char fallthrough.
                bool textEntry = _textEntryMode;
                for (char c : status.word) {
                    UiKey mapped = UiKey::Char;
                    if (!textEntry) {
                        switch (c) {
                            case ';': mapped = UiKey::Up; break;
                            case '.': mapped = UiKey::Down; break;
                            case ',': mapped = UiKey::Left; break;
                            case '/': mapped = UiKey::Right; break;
                            default: break;
                        }
                    }
                    ev.key = mapped;
                    if (mapped == UiKey::Char) ev.ch = c;
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
