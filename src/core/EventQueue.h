#pragma once

#include <cstdint>
#include <cstring>

// Notification payload sent from scan/ tasks to the UI task over a
// FreeRTOS queue.
//
// IMPORTANT: this struct is intentionally plain-old-data (POD) only —
// no String, no std::vector, no pointers into heap objects owned by the
// sender. FreeRTOS queues copy items with a raw memcpy() into internal
// queue storage. If we put an Arduino String or std::vector<PortResult>
// in here, the memcpy would duplicate its internal heap pointer, and the
// moment the sender's local variable goes out of scope its destructor
// frees that heap memory — leaving the copy sitting in the queue with a
// dangling pointer. The receiver would then read freed memory the next
// time it dequeues. That's a real bug, not a hypothetical one, and it's
// a classic ESP32/FreeRTOS footgun.
//
// The fix used throughout this firmware: the queue only ever carries
// *notifications* ("host N changed", "progress is now X%"). The actual
// HostInfo data (which legitimately owns heap memory via String/vector)
// lives in ScanManager's mutex-protected host table. Whoever needs the
// real data (the UI task) takes the mutex for a short, bounded copy-out
// and releases it immediately — see ScanManager::getHost().

enum class ScanEventType : uint8_t {
    HostChanged,
    ScanStarted,
    ScanProgress,
    ScanFinished,
    LogLine,
};

// Which background scanner posted this notification. ScanManager
// (subnet discovery) and PortScanManager (single-host port scan) share
// one queue/one UiManager, and both reuse HostChanged+an index to mean
// "a new result is available" — but the index means something different
// in each case (a row in ScanManager's host table vs. a row in
// PortScanManager's open-port list). Without this tag, a discovery scan
// still running in the background while the user is looking at port-scan
// results would have its events misread as port results (or vice versa)
// by whichever screen is on top. Screens must check `source` before
// acting on hostIndex.
enum class ScanSource : uint8_t { Discovery, PortScan };

struct ScanNotification {
    ScanSource source = ScanSource::Discovery;
    ScanEventType type = ScanEventType::LogLine;
    int16_t hostIndex = -1;   // meaning depends on `source` — see above
    uint8_t progressPct = 0;  // 0-100, valid for ScanProgress
    char text[40] = {0};      // short status text for LogLine, always NUL-terminated

    void setText(const char* s) {
        strncpy(text, s, sizeof(text) - 1);
        text[sizeof(text) - 1] = '\0';
    }
};
