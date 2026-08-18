#include "PlaybookRunner.h"
#include "AssessmentRunner.h"
#include "DiscoveryRunner.h"
#include "ScanManager.h"
#include "SnmpSweep.h"
#include "DataStoreProbe.h"
#include "IotOtProbe.h"
#include "WardrivingManager.h"
#include "BeaconProbeSniffer.h"
#include "../net/WifiManager.h"

PlaybookRunner g_playbookRunner;

namespace {

// Free-function wrappers so every step, regardless of which manager it
// drives, fits one uniform (start/isRunning/stop) shape - same reason
// DiscoveryMenuScreen wraps each screen behind a plain Screen* getter
// instead of storing member-function pointers with mismatched types.
void wStartAssessment() { g_assessmentRunner.start(); }
bool wAssessmentRunning() { return g_assessmentRunner.isRunning(); }

void wStartDiscoveryAll() { g_discoveryRunner.start(); }
bool wDiscoveryAllRunning() { return g_discoveryRunner.isRunning(); }

void wStartNetworkScan() { g_scanManager.startDiscoveryScan(); }
bool wNetworkScanRunning() { return g_scanManager.isRunning(); }

void wStartSnmp() { g_snmpSweep.start(); }
bool wSnmpRunning() { return g_snmpSweep.isRunning(); }

void wStartDataStore() { g_dataStoreProbe.start(); }
bool wDataStoreRunning() { return g_dataStoreProbe.isRunning(); }

void wStartIotOt() { g_iotOtProbe.start(); }
bool wIotOtRunning() { return g_iotOtProbe.isRunning(); }

void wStartWardriving() { g_wardrivingManager.start(); }
bool wWardrivingRunning() { return g_wardrivingManager.isRunning(); }
void wStopWardriving() { g_wardrivingManager.stop(); }

void wStartBeaconProbe() { g_beaconProbeSniffer.start(); }
bool wBeaconProbeRunning() { return g_beaconProbeSniffer.isRunning(); }
void wStopBeaconProbe() { g_beaconProbeSniffer.stop(); }

struct PlaybookStep {
    const char* label;
    void (*start)();
    bool (*isRunning)();
    void (*stop)();     // nullptr = runs to completion on its own (one-shot/orchestrator step)
    uint32_t windowMs;  // >0 = fixed timed window, then `stop` is called; 0 = wait for isRunning() to clear
};

struct PlaybookDef {
    const char* name;
    const char* description;
    const PlaybookStep* steps;
    size_t stepCount;
    bool requiresWifi;  // false only for the pure-RF wireless survey below
};

// FULL RECON: the two existing "one button" orchestrators, chained -
// AUTO ASSESS (network scan -> per-host port scan -> HTML report) then
// RUN ALL DISCOVERY (UPnP/mDNS/SNMP/data-store/IoT-OT/LDAP/NTLM sweeps,
// then LAN topology/passive hosts/rogue DHCP/beacon-probe promiscuous
// windows). Nothing chains these two together today - this is the
// closest thing to "just scan everything, in order, once" this
// firmware has.
const PlaybookStep kFullReconSteps[] = {
    {"AUTO ASSESS", wStartAssessment, wAssessmentRunning, nullptr, 0},
    {"RUN ALL DISCOVERY", wStartDiscoveryAll, wDiscoveryAllRunning, nullptr, 0},
};

// QUICK IOT/OT: a fast, non-promiscuous recon that skips the full port
// range and every promiscuous listener - just "what's here" (network
// scan) then the three unauthenticated-access sweeps most relevant to
// an IoT/OT-heavy segment (SNMP, data-store, and the new IoT/OT probe).
const PlaybookStep kIotOtSteps[] = {
    {"NETWORK SCAN", wStartNetworkScan, wNetworkScanRunning, nullptr, 0},
    {"SNMP SWEEP", wStartSnmp, wSnmpRunning, nullptr, 0},
    {"DATASTORE SWEEP", wStartDataStore, wDataStoreRunning, nullptr, 0},
    {"IOT/OT SWEEP", wStartIotOt, wIotOtRunning, nullptr, 0},
};

// WIRELESS SURVEY: the only playbook that needs no LAN connection at
// all - a timed WAR DRIVING window (nearby AP sightings) followed by a
// timed BEACON/PROBE INTEL window (WPS flags, client PNL harvesting).
// Each step gets a fixed, bounded window rather than running forever -
// a "quick sample", not a replacement for actually opening either
// screen for a longer session.
constexpr uint32_t kWirelessWindowMs = 20000;
const PlaybookStep kWirelessSteps[] = {
    {"WAR DRIVING", wStartWardriving, wWardrivingRunning, wStopWardriving, kWirelessWindowMs},
    {"BEACON/PROBE INTEL", wStartBeaconProbe, wBeaconProbeRunning, wStopBeaconProbe, kWirelessWindowMs},
};

const PlaybookDef kPlaybooks[] = {
    {"FULL RECON", "AUTO ASSESS then RUN ALL DISCOVERY", kFullReconSteps, 2, true},
    {"QUICK IOT/OT", "network scan + SNMP/datastore/IoT-OT", kIotOtSteps, 4, true},
    {"WIRELESS SURVEY", "war driving + beacon/probe window", kWirelessSteps, 2, false},
};
constexpr size_t kPlaybookCount = sizeof(kPlaybooks) / sizeof(kPlaybooks[0]);

}  // namespace

size_t PlaybookRunner::playbookCount() { return kPlaybookCount; }

const char* PlaybookRunner::playbookName(size_t index) {
    return (index < kPlaybookCount) ? kPlaybooks[index].name : "";
}

const char* PlaybookRunner::playbookDescription(size_t index) {
    return (index < kPlaybookCount) ? kPlaybooks[index].description : "";
}

size_t PlaybookRunner::playbookStepCount(size_t index) {
    return (index < kPlaybookCount) ? kPlaybooks[index].stepCount : 0;
}

const char* PlaybookRunner::playbookStepLabel(size_t playbookIndex, size_t stepIndex) {
    if (playbookIndex >= kPlaybookCount) return "";
    const PlaybookDef& pb = kPlaybooks[playbookIndex];
    return (stepIndex < pb.stepCount) ? pb.steps[stepIndex].label : "";
}

void PlaybookRunner::begin(QueueHandle_t outQueue) { _outQueue = outQueue; }

bool PlaybookRunner::start(size_t playbookIndex) {
    if (_running) return false;
    if (playbookIndex >= kPlaybookCount) return false;
    _playbookIndex = playbookIndex;
    _currentStep = 0;
    _running = true;
    notify(ScanEventType::ScanStarted);
    if (xTaskCreatePinnedToCore(&PlaybookRunner::taskEntry, "playbook", 4096, this, 1, nullptr, 0) != pdPASS) {
        // Task never started (out of memory) - clear the running flag so
        // the UI doesn't sit on a playbook with nothing driving it.
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return false;
    }
    return true;
}

void PlaybookRunner::stop() { _running = false; }

void PlaybookRunner::taskEntry(void* arg) {
    static_cast<PlaybookRunner*>(arg)->run();
    vTaskDelete(nullptr);
}

void PlaybookRunner::run() {
    const PlaybookDef& pb = kPlaybooks[_playbookIndex];

    if (pb.requiresWifi && !g_wifi.isConnected()) {
        notify("no WiFi - connect first");
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return;
    }

    notify(String("playbook: ") + pb.name);
    for (size_t i = 0; i < pb.stepCount && _running; i++) {
        _currentStep = i;
        const PlaybookStep& step = pb.steps[i];
        notify(String("step ") + String((unsigned)(i + 1)) + "/" + String((unsigned)pb.stepCount) + ": " +
               step.label);
        notify(ScanEventType::ScanProgress, (uint8_t)((i * 100) / pb.stepCount));

        step.start();
        vTaskDelay(pdMS_TO_TICKS(400));  // let it flip its own running flag before we poll it

        if (step.windowMs > 0) {
            uint32_t start = millis();
            while (_running && (millis() - start) < step.windowMs) vTaskDelay(pdMS_TO_TICKS(250));
            if (step.stop) step.stop();
        } else {
            // A non-window step that never flipped its running flag
            // within the settle delay above either failed to start (a
            // busy manager, the shared radio held elsewhere, no eligible
            // targets, out of memory) or genuinely finished that fast.
            // We can't tell those apart from here, but silently treating
            // the step as "done" when it may never have run at all is
            // exactly the ambiguity this notify removes: say so, rather
            // than moving on as if the step had executed.
            if (!step.isRunning()) {
                notify(String("step ") + String((unsigned)(i + 1)) +
                       " did not stay running (finished instantly or failed to start)");
            }
            while (_running && step.isRunning()) vTaskDelay(pdMS_TO_TICKS(250));
        }
    }

    _currentStep = pb.stepCount;
    notify(_running ? "playbook complete" : "cancelled");
    _running = false;
    notify(ScanEventType::ScanFinished, 100);
}

void PlaybookRunner::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Playbook;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

void PlaybookRunner::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Playbook;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}
