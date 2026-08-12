#include "Sound.h"
#include "../core/Config.h"
#include <M5Cardputer.h>
#include <atomic>

namespace {

struct Note {
    uint16_t freq;
    uint16_t ms;
};

// Recreated again, on user request ("con dei sentori di Nightcall" -
// with hints/flavor of Nightcall). Back to an ORIGINAL composition,
// like the very first Fase 17 version, NOT a transcription of anything
// - the whole reason that first version existed was to evoke the vibe
// without reproducing anyone's copyrighted melody note-for-note, and
// that's the rule again here. Keeps the one thing that's actually the
// user's own and freely reusable: the Am-F-C-G (i-VI-III-VII in A
// minor) chord foundation from the sheet music they gave permission to
// use - that specific progression is also extremely common (it's the
// harmonic backbone of a huge number of pop songs, not something
// anyone owns), so building on it is safe on its own merits, not just
// because of the earlier permission.
//
// The "Nightcall sentori" come from technique, not melody-copying: a
// driving pulsing root-fifth bass ostinato under each chord (the
// hallmark of that track's whole sound), a slow, spacious minor-key
// melodic hook drifting over it, and a wistful, half-chromatic
// descending line at the end of each lap before it resolves back down
// to the low root - evoking that song's melancholy synthwave outro
// feel without quoting its actual tune.
constexpr Note kBootLoop[] = {
    // Am - pulsing root/fifth ostinato, then a rising hook held on top
    {110, 180}, {165, 180}, {110, 180}, {165, 180},  // A2 E3 A2 E3 - pulse
    {262, 220}, {330, 220}, {440, 560},              // C4 E4 A4(held) - hook
    // F
    {87, 180}, {131, 180}, {87, 180}, {131, 180},    // F2 C3 F2 C3 - pulse
    {220, 220}, {262, 220}, {349, 560},              // A3 C4 F4(held) - hook
    // C
    {131, 180}, {196, 180}, {131, 180}, {196, 180},  // C3 G3 C3 G3 - pulse
    {330, 220}, {392, 220}, {523, 560},              // E4 G4 C5(held) - hook
    // G
    {98, 180}, {147, 180}, {98, 180}, {147, 180},    // G2 D3 G2 D3 - pulse
    {247, 220}, {294, 220}, {392, 560},              // B3 D4 G4(held) - hook
    // Wistful descending outro, half-chromatic, settling back to the
    // low root before the loop repeats.
    {440, 260}, {392, 260}, {349, 260}, {330, 260}, {294, 300}, {262, 300}, {220, 340},
    {110, 700},  // A2, held - loop resolves here
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
    // loop's longest single note - the ~700ms final outro note) and deletes
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
