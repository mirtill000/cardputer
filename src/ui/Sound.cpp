#include "Sound.h"
#include "../core/Config.h"
#include <M5Cardputer.h>

namespace {

struct Note {
    uint16_t freq;
    uint16_t ms;
};

// Cyberpunk/synthwave boot riff, ~1.5s total: a driving root/fifth
// pulse (the "engine idle" synthwave intro figure) into a rising A
// minor arpeggio with a flattened seventh for a darker color than a
// plain major run, then a descending phrygian-flavored resolve back to
// a held low root - deliberately moodier than a bright fanfare, to
// match the NETRUNNER splash it now plays under. Replaces the earlier
// four-note ascending major arpeggio.
constexpr Note kBootJingle[] = {
    {110, 60},   // A2 - pulse
    {165, 60},   // E3 - pulse
    {110, 60},   // A2 - pulse
    {165, 60},   // E3 - pulse
    {220, 80},   // A3
    {262, 80},   // C4
    {330, 80},   // E4
    {392, 80},   // G4 (flat seventh, not the major-scale G#)
    {440, 140},  // A4 - peak, held
    {349, 90},   // F4 - phrygian passing tone
    {294, 90},   // D4
    {220, 100},  // A3
    {110, 260},  // A2 - held root, engine cuts out
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
