#include "Sound.h"
#include "../core/Config.h"
#include <M5Cardputer.h>
#include <atomic>

namespace {

struct Note {
    uint16_t freq;
    uint16_t ms;
};

// "Nightcall"-inspired original loop, D minor, ~4s/lap — see
// startBootLoop()'s doc comment for why this is a from-scratch
// composition evoking the vibe, not a transcription. Two phrases:
//  - a driving arpeggiated bassline pulse (D2/A2/D3), the Kavinsky-
//    style "engine" ostinato underneath everything else in that track;
//  - a slower, moodier descending melodic hook on top, in D natural
//    minor (D E F G A Bb C);
// then a held low resolve before the loop repeats.
constexpr Note kBootLoop[] = {
    {73, 90},  {110, 90}, {147, 90}, {110, 90},  // D2 A2 D3 A2 - pulse x3
    {73, 90},  {110, 90}, {147, 90}, {110, 90},
    {73, 90},  {110, 90}, {147, 90}, {110, 90},
    {294, 220}, {262, 220}, {233, 220}, {220, 340},  // D4 C4 Bb3 A3 - hook
    {196, 220}, {220, 220}, {233, 340},              // G3 A3 Bb3
    {147, 260}, {110, 420},                          // D3 A2 - resolve, held
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
    g_bootLoopRunning = true;
    xTaskCreatePinnedToCore(&bootLoopTask, "bootmusic", 2048, nullptr, 1, nullptr, 0);
}

void sound::stopBootLoop() {
    // bootLoopTask notices at the next note boundary (worst case, the
    // loop's longest single note - 420ms) and deletes itself; not
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
