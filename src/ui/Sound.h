#pragma once

// Thin wrapper over M5Cardputer.Speaker, gated by
// AppConfig::uiSoundEnabled (declared since Fase 1 but never actually
// read by anything until now).
namespace sound {

// Cyberpunk/synthwave riff (~1.5s) played once, exactly when the boot
// log hands off to the branded NETRUNNER splash (see BootScreen.cpp) -
// blocking for its duration is fine there since nothing on screen
// animates until it's done (the blinking prompt waits for it). No-op if
// uiSoundEnabled is off.
void playBootJingle();

// Single short beep - safe to call from any task (fire-and-forget:
// M5Unified's Speaker driver plays it asynchronously, this call doesn't
// block). Used to flag a newly-found open network during war driving.
// No-op if uiSoundEnabled is off.
void playAlert();

// Two-tone descending alarm - deliberately harsher/more urgent than
// playAlert(): a confirmed working default credential is a stronger,
// more actionable finding than "found an open AP", the one other event
// in this firmware with a sound. Briefly blocks (~400ms, like
// playBootJingle()) to sequence the two tones - fine to call from any
// background task (CredAuditManager's own, specifically), just not the
// UI render task. No-op if uiSoundEnabled is off.
void playCredAlert();

}  // namespace sound
