#pragma once

#include <cstdint>

// Outcome of a manager's start call. The orchestrators (AUTO ASSESS,
// PLAYBOOK, WAR DRIVING) used to call void start functions and then poll
// isRunning(): if the start had silently bailed out (no WiFi, or another
// task's scan already in progress) they went on to read a host table from
// an earlier run, or waited on and consumed somebody else's scan.
enum class StartResult : uint8_t {
    Started,      // this call started the run
    Busy,         // a run was already in progress (owned by another caller)
    NoWifi,       // needs a WiFi connection and there is none
    InvalidArgs,  // empty/inverted range or similar
};

inline const char* startResultLabel(StartResult r) {
    switch (r) {
        case StartResult::Started:     return "started";
        case StartResult::Busy:        return "busy";
        case StartResult::NoWifi:      return "wifi not connected";
        case StartResult::InvalidArgs: return "invalid range";
    }
    return "?";
}
