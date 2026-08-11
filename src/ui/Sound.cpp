#include "Sound.h"
#include "../core/Config.h"
#include <M5Cardputer.h>
#include <atomic>

namespace {

struct Note {
    uint16_t freq;
    uint16_t ms;
};

// User-provided piece (sheet music supplied directly, confirmed as the
// user's own/rights-cleared - NOT the earlier "Nightcall"-inspired
// original this replaces, and unlike that one, this IS meant to
// reproduce the actual piece, as closely as a monophonic buzzer can).
//
// This is a solo-voice ARRANGEMENT of the opening four bars (i-VI-III-
// VII in A minor: Am-F-C-G, the piano score's left-hand harmony under
// a continuous right-hand broken-chord figure), not a literal encoding
// of either hand alone — M5Cardputer.Speaker plays one tone() at a
// time, so both hands had to be condensed into one line. Each bar:
// the left-hand bass root (held), then the right-hand arpeggio read
// off the score low-to-high, then its top note held slightly longer as
// the phrase's melodic peak. Tempo matches the score's marking (quarter
// = 89 BPM: quarter note ≈ 674ms, eighth ≈ 337ms, dotted quarter ≈
// 1011ms, rounded below).
//
// Read from a photographed score, not run through OCR - please flag
// anything that sounds off against the original once you hear it on
// hardware, same as every other build-verified thing in this project.
constexpr Note kBootLoop[] = {
    // Bar 1 - Am (A C E)
    {110, 1010},                                    // A2 - bass root
    {220, 340}, {262, 340}, {330, 340}, {440, 340},  // A3 C4 E4 A4 - arpeggio
    {659, 674},                                      // E5 - melodic peak
    // Bar 2 - F (F A C)
    {87, 1010},                                      // F2 - bass root
    {175, 340}, {220, 340}, {262, 340}, {349, 340},  // F3 A3 C4 F4 - arpeggio
    {440, 674},                                       // A4 - melodic peak
    // Bar 3 - C (C E G)
    {131, 1010},                                     // C3 - bass root
    {262, 340}, {330, 340}, {392, 340}, {523, 340},  // C4 E4 G4 C5 - arpeggio
    {659, 674},                                       // E5 - melodic peak
    // Bar 4 - G (G B D) - held a touch longer, marks the end of the phrase
    {98, 1010},                                       // G2 - bass root
    {196, 340}, {247, 340}, {294, 340}, {392, 340},  // G3 B3 D4 G4 - arpeggio
    {587, 850},                                        // D5 - melodic peak, held
};
constexpr size_t kBootLoopCount = sizeof(kBootLoop) / sizeof(kBootLoop[0]);

std::atomic<bool> g_bootLoopRunning{false};

void bootLoopTask(void*) {
    while (g_bootLoopRunning) {
        for (size_t i = 0; i < kBootLoopCount && g_bootLoopRunning; i++) {
            // Skipping the tone() call (rather than skipping the whole
            // note) when muted keeps the loop's rhythmic position
            // intact, so re-enabling SOUND mid-track resumes in place
            // instead of restarting from the top.
            if (g_config.uiSoundEnabled) M5Cardputer.Speaker.tone(kBootLoop[i].freq, kBootLoop[i].ms);
            delay(kBootLoop[i].ms + 20);
        }
    }
    vTaskDelete(nullptr);
}

}  // namespace

void sound::startBootLoop() {
    if (g_bootLoopRunning) return;
    // Bumped up from M5Unified's default (~128/255, fairly quiet on
    // this board's small speaker); 220 turned out louder than wanted on
    // real hardware, settled on 180. This is a device-wide setting, not
    // per-tone, so it also raises playAlert()/playCredAlert() - no
    // separate reset once the loop stops, since a slightly louder
    // alert/alarm is a reasonable side effect too, not a bug to work
    // around.
    M5Cardputer.Speaker.setVolume(180);
    g_bootLoopRunning = true;
    xTaskCreatePinnedToCore(&bootLoopTask, "bootmusic", 2048, nullptr, 1, nullptr, 0);
}

void sound::stopBootLoop() {
    // bootLoopTask notices at the next note boundary (worst case, the
    // loop's longest single note - the ~1010ms bass root) and deletes
    // itself; not waited on here, so a short tail can still be heard
    // just after this returns. Accepted, not worth blocking the UI task to
    // close that last sliver.
    g_bootLoopRunning = false;
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
