#pragma once

#include <Arduino.h>
#include "EventQueue.h"

// One shared body for the "post a LogLine to the UI queue" helper that
// ~30 managers each had an identical private copy of (build a
// ScanNotification, tag it with the manager's ScanSource, copy the text
// in, non-blocking xQueueSend). Each manager keeps its own notify()
// method so call sites don't change; the method now just forwards here.
namespace scanlog {

inline void post(QueueHandle_t queue, ScanSource source, const char* text) {
    if (!queue) return;
    ScanNotification n;
    n.source = source;
    n.type = ScanEventType::LogLine;
    n.setText(text);
    xQueueSend(queue, &n, 0);
}

inline void post(QueueHandle_t queue, ScanSource source, const String& text) { post(queue, source, text.c_str()); }

}  // namespace scanlog
