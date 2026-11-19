#pragma once

#include <atomic>

// Atomic "claim the running flag" guard for a manager's start() function.
//
// The pattern every start() used to follow was check-then-set:
//
//     if (_running) return false;
//     ... set up state (clear result tables, build a work list) ...
//     _running = true;
//
// _running is a std::atomic<bool>, but the check and the set are two
// separate operations with the whole setup in between, so two tasks
// calling start() at the same moment could both pass the check and both
// run setup + spawn workers (ScanManager::startDiscoveryScan() alone is
// called from the UI, AUTO ASSESS, SENTINEL, PLAYBOOK and WAR DRIVING
// tasks). RunClaim closes that window: the constructor claims the flag
// with a single exchange(true), so exactly one caller wins.
//
//     RunClaim claim(_running);
//     if (!claim.owned()) return false;   // someone else is running
//     ... setup, any early "return false" releases the flag again ...
//     claim.commit();                     // keep _running == true
//
// Only for managers whose worker task is spawned by start() itself. A
// manager whose persistent task polls _running (SENTINEL, WAR DRIVING,
// PMKID SWEEP, the passive sniffers) must not see the flag go true
// before its setup is finished, so those keep the plain set at the end.
class RunClaim {
public:
    explicit RunClaim(std::atomic<bool>& flag) : _flag(flag), _owned(!flag.exchange(true)) {}
    ~RunClaim() {
        if (_owned && !_committed) _flag = false;
    }
    RunClaim(const RunClaim&) = delete;
    RunClaim& operator=(const RunClaim&) = delete;

    bool owned() const { return _owned; }
    void commit() { _committed = true; }

private:
    std::atomic<bool>& _flag;
    bool _owned;
    bool _committed = false;
};
