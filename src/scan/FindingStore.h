#pragma once

#include <IPAddress.h>
#include <vector>
#include "../core/Finding.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// The single source of truth for "the findings" across the whole app.
// See core/Finding.h for why this exists.
//
// Two ways a finding gets in here:
//
//  1. PUSHED (add()): a module that detects something event-shaped —
//     seen once, then gone from its own volatile buffer — records it
//     here so it survives leaving that module's screen and shows up in
//     the rollup. Any module can contribute in one line; deduped by
//     (source, host, title) so a repeated observation refreshes the
//     existing entry instead of piling up.
//
//  2. DERIVED (buildRollup()): some findings are really live properties
//     of the persistent host table (this host has weak creds / a
//     known-vuln banner / a plaintext port) rather than one-off events.
//     Re-deriving those at read time avoids stale duplicates when a
//     re-scan clears a port, so buildRollup() folds them in fresh from
//     g_scanManager every call rather than requiring the discovery path
//     to remember to push them.
//
// buildRollup() returns the MERGE of both, deduped and sorted
// most-severe-first, bounded — exactly what THREATS renders and what the
// HTML report's ATTACK SURFACE section and the JSON export now emit, so
// all three finally agree instead of each aggregating a different subset.
class FindingStore {
public:
    void begin();

    // Push a finding (see #1 above). Thread-safe. detail defaults to
    // title when left empty.
    void add(ScanSource source, FindingCategory category, RiskLevel severity, const IPAddress& host,
             const String& title, const String& detail = String());

    // Drop all pushed findings. Called by Session::beginNew() so a fresh
    // assessment starts clean; the DERIVED half of buildRollup() follows
    // the host table on its own, so it clears when a new scan repopulates
    // that table regardless.
    void clear();

    // How many findings were pushed via add() (NOT the rollup total,
    // which also includes host-table-derived ones — use buildRollup for
    // that). Mostly for the ACTIVITY/status surfaces.
    size_t pushedCount() const;

    // The merged, deduped, severity-sorted, bounded list. `out` is
    // cleared first. Safe to call every frame (THREATS does).
    void buildRollup(std::vector<Finding>& out) const;

    static constexpr size_t kMaxPushed = 48;   // ceiling on pushed findings held in RAM
    static constexpr size_t kRollupMax = 48;   // ceiling on the merged rollup buildRollup returns

private:
    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Finding> _pushed;
};

extern FindingStore g_findings;
