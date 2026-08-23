#include "Sound.h"
#include "../core/Config.h"
#include <M5Cardputer.h>
#include <atomic>

namespace {

struct Note {
    uint16_t freq;
    uint16_t ms;
};

// Refreshed again on user request - they pointed at the driving section
// of Nightcall from ~1:10 (the fuller, more insistent part) and asked
// for that energy. Still an ORIGINAL composition, NOT a transcription:
// the rule from the first Fase 17 version holds - evoke the vibe through
// technique, never quote the copyrighted melody note-for-note. What
// changed vs. the previous loop is the *feel*, not the source material:
// the lazy root/fifth pulse became a relentless octave-pulse ostinato
// (root-octave-root-fifth, eighth after eighth) to get that hypnotic
// forward drive, with a tighter arpeggiated hook riding on top.
//
// Harmony is still the freely-reusable Am-F-C-G (i-VI-III-VII in A
// minor) foundation - both because the user gave permission to build on
// that specific progression from their sheet music, and because it's one
// of the most common progressions in pop music, owned by no one. The
// "Nightcall sentori" come from arrangement (pulsing octave bass, minor
// key, a wistful half-chromatic descent resolving to the low root),
// never from the actual tune.
constexpr Note kBootLoop[] = {
    // Am - relentless octave-pulse ostinato (A2 A3 A2 E3 ...), tight arp hook on top
    {110, 120}, {220, 120}, {110, 120}, {165, 120}, {110, 120}, {220, 120}, {165, 120}, {110, 120},
    {330, 180}, {440, 180}, {523, 180}, {440, 360},  // E4 A4 C5 A4(held) - hook
    // F
    {87, 120}, {175, 120}, {87, 120}, {131, 120}, {87, 120}, {175, 120}, {131, 120}, {87, 120},
    {262, 180}, {349, 180}, {440, 180}, {349, 360},  // C4 F4 A4 F4(held) - hook
    // C
    {131, 120}, {262, 120}, {131, 120}, {196, 120}, {131, 120}, {262, 120}, {196, 120}, {131, 120},
    {330, 180}, {392, 180}, {523, 180}, {392, 360},  // E4 G4 C5 G4(held) - hook
    // G
    {98, 120}, {196, 120}, {98, 120}, {147, 120}, {98, 120}, {196, 120}, {147, 120}, {98, 120},
    {294, 180}, {392, 180}, {494, 180}, {392, 360},  // D4 G4 B4 G4(held) - hook
    // Driving climb, then a wistful half-chromatic descent settling back
    // to the low root before the loop repeats.
    {440, 200}, {494, 200}, {523, 240}, {494, 240}, {440, 240},
    {392, 260}, {349, 260}, {330, 300}, {294, 320}, {262, 340},
    {110, 640},  // A2, held - loop resolves here
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
    // loop's longest single note - the ~640ms final outro note) and deletes
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

void sound::playDone() {
    if (!g_config.uiSoundEnabled) return;
    M5Cardputer.Speaker.tone(2200, 90);  // async, non-blocking - safe on the UI task
}
