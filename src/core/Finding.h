#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include "Types.h"       // RiskLevel
#include "EventQueue.h"  // ScanSource

// The one shared "finding" record every module contributes to, so the
// app produces a single assessment instead of ~40 disconnected result
// lists. Before this, each scanner/sweep kept its results in a private
// std::vector inside its own screen, and only THREATS (by reaching into
// six managers by hand) and the HTML report (by re-deriving from the
// host table) ever aggregated anything — everything else was invisible
// the moment you left its screen. FindingStore (scan/FindingStore.h) is
// now the single source of truth those two, plus the JSON export, all
// read from.
//
// Kept deliberately small and POD-ish (String is fine here — unlike
// ScanNotification this never travels through a raw FreeRTOS queue
// memcpy; it lives in a mutex-protected std::vector like the host
// table).

// Coarse bucket for a finding, independent of its severity: it answers
// "what KIND of thing is this" (so the report/THREATS can group), where
// RiskLevel answers "how bad". A plaintext service is an Exposure at
// Warning; a confirmed default credential is a Weakness at Critical; a
// deauth flood in progress is a Threat at Critical.
enum class FindingCategory : uint8_t {
    Recon,     // benign discovery: a host/service/banner simply exists
    Exposure,  // reachable when it probably shouldn't be: no-auth service, plaintext login, anon share
    Weakness,  // an exploitable weakness: default/weak creds, known-vuln banner, WPS unlocked
    Threat,    // active malicious/anomalous behavior observed: rogue DHCP, deauth flood, new device
};

struct Finding {
    ScanSource source = ScanSource::Discovery;
    FindingCategory category = FindingCategory::Recon;
    RiskLevel severity = RiskLevel::Warning;
    IPAddress host;         // 0.0.0.0 => network-wide / not tied to one host
    String title;           // short one-liner for the rollup row (e.g. "192.168.1.5 telnet open")
    String detail;          // longer text for the I: overlay / report ("" => same as title)
    uint32_t whenMs = 0;    // millis() when first observed

    bool hostIsSet() const { return (uint32_t)host != 0; }
};

// Category label for reports/detail views. Kept next to the enum so a
// new category can't be added without a label.
inline const char* findingCategoryLabel(FindingCategory c) {
    switch (c) {
        case FindingCategory::Recon:    return "RECON";
        case FindingCategory::Exposure: return "EXPOSURE";
        case FindingCategory::Weakness: return "WEAKNESS";
        case FindingCategory::Threat:   return "THREAT";
    }
    return "?";
}
