#pragma once

#include <Arduino.h>
#include <cstdint>

// A lightweight "assessment session": the one anchor that ties a
// discovery run, the tools run against that target afterwards, the
// findings they raise and the report/export they produce into a single
// numbered assessment instead of a loose pile of independent actions.
//
// Kept deliberately tiny and dependency-free (no scan/ or storage/
// includes) so it can sit in core/ and be referenced from anywhere. It
// does NOT itself touch the finding store — beginNew() returns true when
// a genuinely new session started and the caller (ScanManager) clears
// findings on that edge, so the layering stays one-way (scan/ depends on
// core/, never the reverse).
class Session {
public:
    // Starts a new assessment for `target` (the network SSID being
    // assessed). No-op — returns false — when an assessment for the SAME
    // target is already in progress, so repeated/rescan and SENTINEL's
    // own periodic scans of the network you're on keep accumulating into
    // one session rather than resetting it every cycle. Returns true only
    // on the edge where a new session actually began (first scan after
    // boot, or the target changed), which is the caller's cue to clear
    // the previous session's findings.
    bool beginNew(const String& target);

    uint32_t id() const { return _id; }             // 0 until the first beginNew()
    const String& target() const { return _target; }
    uint32_t startedMs() const { return _startedMs; }
    bool active() const { return _id != 0; }

private:
    uint32_t _id = 0;
    String _target;
    uint32_t _startedMs = 0;
};

extern Session g_session;
