#include "Sound.h"
#include "../core/Config.h"
#include <M5Cardputer.h>

namespace {

struct Note {
    uint16_t freq;
    uint16_t ms;
};

// Short ascending arpeggio - four notes, deliberately brief (well under
// a second total) since this plays once every single boot and
// shouldn't get old.
constexpr Note kBootJingle[] = {
    {523, 90},    // C5
    {659, 90},    // E5
    {784, 90},    // G5
    {1047, 180},  // C6
};

}  // namespace

void sound::playBootJingle() {
    if (!g_config.uiSoundEnabled) return;
    for (const auto& n : kBootJingle) {
        M5Cardputer.Speaker.tone(n.freq, n.ms);
        delay(n.ms + 20);  // small gap so notes don't blur together
    }
}

void sound::playAlert() {
    if (!g_config.uiSoundEnabled) return;
    M5Cardputer.Speaker.tone(1800, 120);
}

void sound::playCredAlert() {
    if (!g_config.uiSoundEnabled) return;
    M5Cardputer.Speaker.tone(1400, 160);
    delay(180);
    M5Cardputer.Speaker.tone(600, 260);
    delay(280);
}
