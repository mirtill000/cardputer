#pragma once

#include <Arduino.h>
#include <atomic>
#include <cstddef>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// "Playbook": a small library of scriptable, unattended scan sequences -
// pick one preset, press start, walk away. Same principle as
// DiscoveryRunner/AssessmentRunner/PmkidSweepManager: this only ever
// DRIVES other managers' public start()/isRunning()/stop() APIs (or, for
// the two steps that are themselves one-button orchestrators, their own
// start()/isRunning()), and reimplements nothing. Think of it as a
// meta-orchestrator: a playbook step can be a single probe, or an
// entire existing orchestrator run as one step.
//
// Not a real scripting language on purpose - this device has no
// filesystem-loaded script format, and hand-rolling one (parser,
// interpreter, error handling for malformed scripts) would be a large
// amount of new surface for a feature whose actual job is "chain a
// handful of things I'd otherwise do by hand, in order, unattended".
// The presets below cover the combinations that don't already exist:
// AUTO ASSESS and RUN ALL DISCOVERY are each already a "one button"
// sequence on their own, but nothing chains the two together, or
// chains a WiFi-connection-free wireless-only sequence, until now.
class PlaybookRunner {
public:
    void begin(QueueHandle_t outQueue);

    static size_t playbookCount();
    static const char* playbookName(size_t index);
    static const char* playbookDescription(size_t index);
    static size_t playbookStepCount(size_t index);
    static const char* playbookStepLabel(size_t playbookIndex, size_t stepIndex);

    bool start(size_t playbookIndex);  // no-op if already running or index is out of range
    void stop();                       // request cancellation; finishes the current step, then bails
    bool isRunning() const { return _running; }

    size_t currentPlaybook() const { return _playbookIndex; }
    size_t currentStep() const { return _currentStep; }  // == that playbook's stepCount once done

private:
    static void taskEntry(void* arg);
    void run();
    void notify(const String& text);
    void notify(ScanEventType type, uint8_t pct = 0);

    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
    std::atomic<size_t> _playbookIndex{0};
    std::atomic<size_t> _currentStep{0};
};

extern PlaybookRunner g_playbookRunner;
