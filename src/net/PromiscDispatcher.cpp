#include "PromiscDispatcher.h"

#include <Arduino.h>
#include <esp_wifi.h>
#include <atomic>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

constexpr int kMaxSubscribers = 12;
constexpr UBaseType_t kQueueDepth = 8;  // 8 x ~1KB, allocated on first subscribe only
constexpr uint32_t kTaskStack = 6144;   // runs every subscriber's handler

struct Slot {
    uint32_t mask = 0;
    promisc::Handler handler = nullptr;
};

Slot g_slots[kMaxSubscribers];
int g_active = 0;
SemaphoreHandle_t g_lock = nullptr;  // guards g_slots, g_active and the queue/task creation
QueueHandle_t g_queue = nullptr;     // written once (under g_lock) before the callback is installed
std::atomic<uint32_t> g_mask{0};     // union of subscriber masks, read lock-free by the callback
std::atomic<uint32_t> g_dropped{0};

void recomputeMaskLocked() {
    uint32_t m = 0;
    for (const Slot& s : g_slots) {
        if (s.handler) m |= s.mask;
    }
    g_mask = m;
}

// WiFi driver task context: copy and return, nothing else.
void rxCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (!(g_mask.load(std::memory_order_relaxed) & (1u << (uint32_t)type))) return;
    auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
    if (!pkt || !g_queue) return;

    // Static, not on the stack: ~1KB would be a lot for the driver task,
    // and this callback is only ever invoked from that one task.
    static promisc::Frame frame;
    uint16_t len = pkt->rx_ctrl.sig_len;
    frame.origLen = len;
    frame.len = (len > promisc::kSnapLen) ? promisc::kSnapLen : len;
    frame.rssi = (int8_t)pkt->rx_ctrl.rssi;
    frame.channel = (uint8_t)pkt->rx_ctrl.channel;
    frame.type = (uint8_t)type;
    memcpy(frame.data, pkt->payload, frame.len);
    if (xQueueSend(g_queue, &frame, 0) != pdTRUE) g_dropped++;
}

void dispatchTask(void*) {
    static promisc::Frame frame;  // task-private
    Slot targets[kMaxSubscribers];
    for (;;) {
        if (xQueueReceive(g_queue, &frame, portMAX_DELAY) != pdTRUE) continue;

        // Snapshot the matching handlers, then call them without the lock
        // so a slow handler never blocks subscribe()/unsubscribe().
        int n = 0;
        uint32_t bit = 1u << frame.type;
        if (xSemaphoreTake(g_lock, portMAX_DELAY) == pdTRUE) {
            for (const Slot& s : g_slots) {
                if (s.handler && (s.mask & bit)) targets[n++] = s;
            }
            xSemaphoreGive(g_lock);
        }
        for (int i = 0; i < n; i++) targets[i].handler(frame);
    }
}

}  // namespace

void promisc::begin() {
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
}

int promisc::subscribe(uint32_t typeMask, Handler handler) {
    if (!g_lock || !handler || !typeMask) return -1;
    if (xSemaphoreTake(g_lock, portMAX_DELAY) != pdTRUE) return -1;

    if (!g_queue) {
        g_queue = xQueueCreate(kQueueDepth, sizeof(Frame));
        if (!g_queue) {
            xSemaphoreGive(g_lock);
            return -1;
        }
        if (xTaskCreatePinnedToCore(&dispatchTask, "promisc", kTaskStack, nullptr, 2, nullptr, 0) != pdPASS) {
            vQueueDelete(g_queue);
            g_queue = nullptr;
            xSemaphoreGive(g_lock);
            return -1;
        }
    }

    int id = -1;
    for (int i = 0; i < kMaxSubscribers; i++) {
        if (!g_slots[i].handler) {
            id = i;
            break;
        }
    }
    if (id < 0) {
        xSemaphoreGive(g_lock);
        return -1;
    }

    g_slots[id].mask = typeMask;
    g_slots[id].handler = handler;
    recomputeMaskLocked();

    // (Re)install and (re)enable on EVERY subscribe, not only the first:
    // an exclusive-mode manager (SENTINEL, deauth/PMKID capture, ARP) may
    // have replaced the callback since, and its stop() turns promiscuous
    // mode off globally - with g_active still > 0 here, a first-only
    // enable would leave every later subscriber listening to nothing.
    // Both calls are idempotent.
    esp_wifi_set_promiscuous_rx_cb(&rxCallback);
    esp_wifi_set_promiscuous(true);
    g_active++;

    xSemaphoreGive(g_lock);
    return id;
}

void promisc::unsubscribe(int id) {
    if (!g_lock || id < 0 || id >= kMaxSubscribers) return;
    if (xSemaphoreTake(g_lock, portMAX_DELAY) != pdTRUE) return;
    if (g_slots[id].handler) {
        g_slots[id] = Slot{};
        recomputeMaskLocked();
        if (--g_active == 0) esp_wifi_set_promiscuous(false);
    }
    xSemaphoreGive(g_lock);
}

size_t promisc::subscriberCount() {
    if (!g_lock || xSemaphoreTake(g_lock, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    size_t n = (size_t)g_active;
    xSemaphoreGive(g_lock);
    return n;
}

uint32_t promisc::droppedFrames() { return g_dropped; }
