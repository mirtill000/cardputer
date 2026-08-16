#pragma once

#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Unauthenticated IoT/OT protocol detection: sweeps the alive-host list
// and checks the protocols most commonly found exposed with no
// authentication on IoT/OT-adjacent network segments:
//   MQTT (1883/TCP), Modbus TCP (502/TCP), CoAP (5683/UDP),
//   BACnet/IP (47808/UDP), DNP3 (20000/TCP).
// Same risk tier and design as DataStoreProbe (read-only, no gate) -
// this is the same kind of finding (unauthenticated data-plane access),
// just for the protocol family that shows up on building-automation/
// SCADA/consumer-IoT segments instead of application servers.
//
// MQTT: sends a real CONNECT packet with clean-session and no
// credentials; a CONNACK with return code 0 means the broker accepted
// an anonymous client — immediately DISCONNECTs afterward, never
// publishes or subscribes to anything.
// Modbus TCP: sends a Read Device Identification request (function
// 0x2B/0x0E, object 0 = VendorName) — read-only by definition; Modbus
// itself has NO authentication concept at all, so any valid response
// (including an exception reply, if Read Device ID specifically isn't
// supported) is itself the finding: an OT protocol design that can
// never require a password is reachable from this segment.
// CoAP: sends a NON-confirmable GET to the standard CoRE resource-
// discovery path (/.well-known/core, RFC 6690) — the read-only
// "what do you offer" request every CoAP client is expected to be able
// to send.
// BACnet/IP: sends a unicast Who-Is (BVLC Original-Unicast-NPDU,
// Unconfirmed-Request/Who-Is) — the same benign "who's out there"
// broadcast every BACnet workstation issues, just addressed to one
// host instead of the whole segment; a structurally valid I-Am reply
// confirms a live BACnet device. Like Modbus, BACnet has no mandatory
// authentication for this exchange.
// DNP3: sends a Data Link Layer Link Status Request (function code 9,
// primary/from-master, broadcast destination address 0xFFFF — same
// technique nmap's dnp3-info script uses) with a correctly-computed
// DNP3 CRC-16. DNP3's Data Link Layer itself has no authentication
// (Secure Authentication is an optional, rarely-deployed add-on), so
// any reply starting with the DNP3 sync bytes (0x05 0x64) is treated as
// live — response CONTENT is deliberately not validated further, same
// "a reply at all is strong enough evidence" principle UdpProbe uses,
// to avoid false negatives from cross-vendor reply-format variance.
class IotOtProbe {
public:
    struct Finding {
        IPAddress ip;
        String service;  // "mqtt" / "modbus" / "coap" / "bacnet" / "dnp3"
        String detail;   // vendor/resource string or a short note
        bool noAuth = false;  // answered without requiring any credential
    };

    void begin(QueueHandle_t outQueue);

    bool start();  // sweeps current alive hosts; no-op if already running
    bool isRunning() const { return _running; }
    uint8_t progressPct() const { return _progressPct; }

    size_t count() const;
    bool get(size_t index, Finding& out) const;

private:
    static constexpr size_t kMaxFindings = 40;
    static constexpr uint16_t kConnectTimeoutMs = 700;
    static constexpr uint16_t kReadTimeoutMs = 700;

    static void taskEntry(void* arg);
    void run();
    void probeHost(const IPAddress& ip);
    void probeMqtt(const IPAddress& ip);
    void probeModbus(const IPAddress& ip);
    void probeCoap(const IPAddress& ip);
    void probeBacnet(const IPAddress& ip);
    void probeDnp3(const IPAddress& ip);
    void addFinding(const IPAddress& ip, const char* service, const String& detail, bool noAuth);
    void notify(const String& text);
    void notify(ScanEventType type, uint8_t pct = 0);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Finding> _findings;
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
    std::atomic<uint8_t> _progressPct{0};
};

extern IotOtProbe g_iotOtProbe;
