#pragma once

// Thin wrapper over M5Cardputer.Speaker, gated by
// AppConfig::uiSoundEnabled (declared since Fase 1 but never actually
// read by anything until now).
namespace sound {

// Starts a looping ~13s theme (see kBootLoop in Sound.cpp) on its own
// background FreeRTOS task, repeating until stopBootLoop() is called.
// A solo-voice arrangement of a piece the user supplied as sheet music
// (confirmed as their own/rights-cleared before this was written) -
// condensed to one monophonic line since M5Cardputer.Speaker only ever
// plays one tone() at a time and the original is a two-hand piano
// piece. Runs on the splash screen only (BootScreen starts it once the
// title is revealed, stops it in onExit() — see BootScreen.cpp) —
// non-blocking, so it's safe to call from the UI render task unlike
// the other functions here. No-op (task never even starts) if already
// running; if uiSoundEnabled is OFF, the loop still runs and keeps
// time silently (tone() calls skipped) so toggling sound back on
// mid-track resumes in the right rhythmic position instead of
// restarting.
void startBootLoop();
void stopBootLoop();

// Single short beep - safe to call from any task (fire-and-forget:
// M5Unified's Speaker driver plays it asynchronously, this call doesn't
// block). Used to flag a newly-found open network during war driving.
// No-op if uiSoundEnabled is off.
void playAlert();

// Two-tone descending alarm - deliberately harsher/more urgent than
// playAlert(): a confirmed working default credential is a stronger,
// more actionable finding than "found an open AP", the one other event
// in this firmware with a sound. Briefly blocks (~400ms) to sequence
// the two tones - fine to call from any background task
// (CredAuditManager's own, specifically), just not the UI render task.
// No-op if uiSoundEnabled is off.
void playCredAlert();

}  // namespace sound
