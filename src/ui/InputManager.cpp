#include "InputManager.h"
#include <M5Cardputer.h>

namespace {
constexpr UBaseType_t kQueueLen = 16;
constexpr TickType_t kPollDelay = pdMS_TO_TICKS(20);  // ~50Hz poll, well under human reaction time
constexpr uint32_t kRepeatInitialMs = 380;  // hold a nav key this long before the first auto-repeat
constexpr uint32_t kRepeatIntervalMs = 90;  // then repeat at ~11Hz while it stays held
}  // namespace

void InputManager::begin() {
    _queue = xQueueCreate(kQueueLen, sizeof(UiKeyEvent));
    xTaskCreatePinnedToCore(&InputManager::taskEntry, "input", 3072, this, 3, nullptr, 1);
}

void InputManager::taskEntry(void* arg) {
    static_cast<InputManager*>(arg)->run();
}

bool InputManager::decodeKey(UiKeyEvent& ev) const {
    auto status = M5Cardputer.Keyboard.keysState();
    ev = UiKeyEvent{};

    // The Cardputer has no dedicated arrow keys; the small inverted-T
    // cluster on the bottom row (',' ';' '.' '/') doubles as
    // Left/Up/Down/Right in most M5Stack examples and games, so we
    // follow that convention here.
    if (status.enter) {
        ev.key = UiKey::Enter;
    } else if (status.del) {
        ev.key = UiKey::Back;
    } else if (status.tab) {
        ev.key = UiKey::Tab;
    } else {
        // Space isn't special-cased: it arrives as ' ' in status.word
        // like any other character, handled by the Char fallthrough.
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

    return ev.key != UiKey::None;
}

bool InputManager::isRepeatable(const UiKeyEvent& ev) {
    // Only the navigation arrows auto-repeat while held. Repeating
    // Enter/Back/Tab or a text character would be surprising (and, for
    // Enter, potentially destructive) - a held letter in a text field
    // must not machine-gun either.
    switch (ev.key) {
        case UiKey::Up:
        case UiKey::Down:
        case UiKey::Left:
        case UiKey::Right:
            return true;
        default:
            return false;
    }
}

void InputManager::run() {
    for (;;) {
        M5Cardputer.update();

        bool pressed = M5Cardputer.Keyboard.isPressed();
        bool changed = M5Cardputer.Keyboard.isChange();

        if (changed && pressed) {
            // A new/changed keypress - decode and emit it, then arm
            // auto-repeat if it's a navigation key.
            UiKeyEvent ev;
            if (decodeKey(ev)) {
                xQueueSend(_queue, &ev, 0);
                if (isRepeatable(ev)) {
                    _repeatEv = ev;
                    _repeatActive = true;
                    _nextRepeatMs = millis() + kRepeatInitialMs;
                } else {
                    _repeatActive = false;
                }
            } else {
                _repeatActive = false;
            }
        } else if (!pressed) {
            // Everything released - disarm.
            _repeatActive = false;
        } else if (_repeatActive && (int32_t)(millis() - _nextRepeatMs) >= 0) {
            // Same navigation key still held with no change event - repeat
            // it so holding an arrow scrolls a long list.
            xQueueSend(_queue, &_repeatEv, 0);
            _nextRepeatMs = millis() + kRepeatIntervalMs;
        }

        vTaskDelay(kPollDelay);
    }
}
