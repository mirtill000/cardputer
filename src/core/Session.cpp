#include "Session.h"

Session g_session;

bool Session::beginNew(const String& target) {
    // Same target already being assessed: keep the current session so a
    // rescan (or SENTINEL's periodic internal scans) accumulates rather
    // than resetting. active() guards the first-ever call, where _target
    // is "" and the real target may legitimately also be "" (no SSID).
    if (active() && target == _target) return false;
    _id++;
    _target = target;
    _startedMs = millis();
    return true;
}
