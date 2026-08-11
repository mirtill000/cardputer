#include "Sound.h"
#include "../core/Config.h"
#include <M5Cardputer.h>
#include <atomic>

namespace {

struct Note {
    uint16_t freq;
    uint16_t ms;
};

// "Nightcall"-inspired original loop, D minor, ~6.5s/lap — see
// startBootLoop()'s doc comment for why this is a from-scratch
// composition evoking the vibe, not a transcription. Slowed down and
// darkened from the first version on user feedback ("more cyberpunk,
// slower tempo"): every note runs longer, and the hook now passes
// through Ab3 - a tritone against the D minor root, the classic
// "dissonant, ominous" synthwave color - before resolving up, plus a
// bright octave-up lead stab (D5/A4) for contrast against how low and
// slow everything else sits. Four phrases:
//  - a heavy, spaced-out arpeggiated bassline pulse (D2/A2/D3), the
//    Kavinsky-style "engine" ostinato underneath everything else in
//    that track;
//  - a slower, moodier melodic hook through the tritone, in D natural
//    minor (D E F G A Bb C) plus that one chromatic passing tone;
//  - a brief bright lead flourish, an octave up, cutting through;
// then a held low resolve before the loop repeats.
constexpr Note kBootLoop[] = {
    {73, 140}, {110, 140}, {147, 140}, {110, 140},  // D2 A2 D3 A2 - pulse x3
    {73, 140}, {110, 140}, {147, 140}, {110, 140},
    {73, 140}, {110, 140}, {147, 140}, {110, 140},
    {294, 320}, {262, 320}, {233, 320}, {208, 260},  // D4 C4 Bb3 Ab3 (tritone)
    {220, 480},                                      // A3 - resolves up out of the tritone
    {196, 320}, {220, 320}, {233, 480},               // G3 A3 Bb3
    {587, 200}, {440, 260},                           // D5 A4 - bright lead stab
    {147, 380}, {110, 620},                           // D3 A2 - resolve, held
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
    // loop's longest single note - 620ms) and deletes itself; not
    // waited on here, so a very short tail can still be heard just
    // after this returns. Accepted, not worth blocking the UI task to
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
