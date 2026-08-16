#include "IotOtProbe.h"
#include "ScanManager.h"
#include "../core/Types.h"
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <cstring>

IotOtProbe g_iotOtProbe;

namespace {

// Reads up to maxLen bytes for up to timeoutMs over TCP, returning
// whatever arrived (stops early once the peer pauses after sending
// something) — same helper as DataStoreProbe's readSome().
size_t readSome(WiFiClient& c, uint16_t timeoutMs, uint8_t* buf, size_t maxLen) {
    size_t got = 0;
    uint32_t start = millis();
    while ((millis() - start) < timeoutMs && got < maxLen) {
        if (c.available()) {
            buf[got++] = (uint8_t)c.read();
        } else if (got > 0) {
            break;
        } else {
            delay(5);
        }
    }
    return got;
}

// DNP3's Data Link Layer CRC-16 (IEEE 1815 Annex): polynomial 0xA6BC in
// reversed/LSB-first form, initial value 0, one's-complemented result -
// the same well-published algorithm every open DNP3 stack (opendnp3,
// pydnp3, ...) implements. Operates over one block at a time (<=16
// bytes); this firmware only ever sends a 6-byte header block, so no
// multi-block chunking is needed. NOT verified against a real DNP3
// outstation in this environment (no hardware/simulator available here
// - see README "Limiti noti") - if the CRC is wrong, compliant devices
// silently drop the frame, which fails safe (a missed finding, never a
// false one) rather than corrupting anything on the wire.
uint16_t dnp3Crc(const uint8_t* data, size_t len) {
    uint16_t crc = 0x0000;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x0001) {
                crc = (uint16_t)((crc >> 1) ^ 0xA6BC);
            } else {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return (uint16_t)(~crc);
}

}  // namespace

void IotOtProbe::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

bool IotOtProbe::start() {
    if (_running) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _findings.clear();
        xSemaphoreGive(_mutex);
    }
    _progressPct = 0;
    _running = true;
    notify(ScanEventType::ScanStarted);
    xTaskCreatePinnedToCore(&IotOtProbe::taskEntry, "iotot", 6144, this, 1, nullptr, 0);
    return true;
}

void IotOtProbe::taskEntry(void* arg) {
    static_cast<IotOtProbe*>(arg)->run();
    vTaskDelete(nullptr);
}

void IotOtProbe::run() {
    std::vector<IPAddress> targets;
    size_t n = g_scanManager.hostCount();
    HostInfo h;
    for (size_t i = 0; i < n; i++) {
        if (g_scanManager.getHost(i, h) && h.alive) targets.push_back(h.ip);
    }

    if (targets.empty()) {
        notify("no alive hosts - run NETWORK SCAN first");
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return;
    }

    for (size_t i = 0; i < targets.size() && _running; i++) {
        probeHost(targets[i]);
        _progressPct = (uint8_t)(((i + 1) * 100) / targets.size());
        notify(ScanEventType::ScanProgress, _progressPct);
    }

    notify(String((unsigned)count()) + " IoT/OT finding(s)");
    _running = false;
    notify(ScanEventType::ScanFinished, 100);
}

void IotOtProbe::probeHost(const IPAddress& ip) {
    probeMqtt(ip);
    probeModbus(ip);
    probeCoap(ip);
    probeBacnet(ip);
    probeDnp3(ip);
}

void IotOtProbe::probeMqtt(const IPAddress& ip) {
    WiFiClient c;
    if (!c.connect(ip, 1883, kConnectTimeoutMs)) return;

    // MQTT 3.1.1 CONNECT: clean session, no username/password/will, a
    // throwaway client ID, 60s keepalive - the same handshake a real
    // MQTT client sends before it can do anything else at all.
    static const uint8_t kConnect[] = {
        0x10, 20,                               // CONNECT, remaining length 20
        0x00, 0x04, 'M', 'Q', 'T', 'T',          // protocol name "MQTT"
        0x04,                                    // protocol level (3.1.1)
        0x02,                                    // connect flags: clean session only
        0x00, 0x3C,                              // keep-alive: 60s
        0x00, 0x08, 'c', 'p', '-', 'p', 'r', 'o', 'b', 'e',  // client ID
    };
    c.write(kConnect, sizeof(kConnect));

    uint8_t reply[8] = {0};
    size_t got = readSome(c, kReadTimeoutMs, reply, sizeof(reply));
    if (got >= 4 && reply[0] == 0x20) {  // CONNACK
        uint8_t returnCode = reply[3];
        if (returnCode == 0x00) {
            // Accepted with no credentials at all - immediately
            // disconnect cleanly rather than leaving a session open on
            // the broker.
            static const uint8_t kDisconnect[] = {0xE0, 0x00};
            c.write(kDisconnect, sizeof(kDisconnect));
            addFinding(ip, "mqtt", "broker accepts anonymous connect", true);
        } else if (returnCode == 0x04 || returnCode == 0x05) {
            addFinding(ip, "mqtt", "auth required", false);
        }
    }
    c.stop();
}

void IotOtProbe::probeModbus(const IPAddress& ip) {
    WiFiClient c;
    if (!c.connect(ip, 502, kConnectTimeoutMs)) return;

    // MBAP header (transaction id, protocol id 0, length, unit id) +
    // Read Device Identification (function 0x2B, MEI type 0x0E, "Basic"
    // read-device-id code, starting at object 0 = VendorName). Read-only
    // by definition - Modbus has no write involved in this request at
    // all. Modbus itself has no authentication concept whatsoever, so
    // ANY valid reply (success or a protocol exception) already is the
    // finding: an OT protocol that can never require a password is
    // reachable from here.
    static const uint8_t kReadDeviceId[] = {
        0x00, 0x01,  // transaction id
        0x00, 0x00,  // protocol id (Modbus)
        0x00, 0x05,  // length: unit id + function + MEI type + code + object id
        0x01,        // unit id
        0x2B,        // function: Encapsulated Interface Transport
        0x0E,        // MEI type: Read Device Identification
        0x01,        // read device id code: Basic
        0x00,        // object id: VendorName
    };
    c.write(kReadDeviceId, sizeof(kReadDeviceId));

    uint8_t reply[64] = {0};
    size_t got = readSome(c, kReadTimeoutMs, reply, sizeof(reply));
    if (got >= 8) {
        uint8_t funcCode = reply[7];
        if (funcCode == 0x2B && got >= 16) {
            // reply[8..13]: MEI type, read-dev-id code, conformity,
            // more-follows, next-object, number-of-objects; reply[14..]
            // is the first {objectId, objectLen, objectValue...} triple.
            // First object is VendorName if the device populated it.
            uint8_t numObjects = reply[13];
            if (numObjects > 0 && reply[14] == 0x00) {
                uint8_t objLen = reply[15];
                size_t avail = (got > 16) ? (got - 16) : 0;
                if (objLen > avail) objLen = (uint8_t)avail;
                String vendor;
                for (uint8_t k = 0; k < objLen; k++) vendor += (char)reply[16 + k];
                addFinding(ip, "modbus", vendor.length() ? vendor : String("responds (no vendor string)"), true);
            } else {
                addFinding(ip, "modbus", "responds (device id unreadable)", true);
            }
        } else if (funcCode == 0xAB) {
            // Exception reply to Read Device ID specifically - the
            // device IS a live Modbus TCP endpoint, it just doesn't
            // support this particular function.
            addFinding(ip, "modbus", "responds (device id not supported)", true);
        }
    }
    c.stop();
}

void IotOtProbe::probeCoap(const IPAddress& ip) {
    WiFiUDP udp;
    if (!udp.begin(0)) return;  // bind an ephemeral local port

    // CoAP NON-confirmable GET /.well-known/core (RFC 6690) - the
    // standard, read-only "what resources do you offer" discovery
    // request. NON (not CON) so this firmware never has to implement
    // CoAP's ACK/retransmission machinery for a one-shot probe.
    static const uint8_t kGetWellKnownCore[] = {
        0x50, 0x01, 0x00, 0x01,  // Ver=1, Type=NON, TKL=0 | Code=0.01 GET | Message ID
        0xBB, '.', 'w', 'e', 'l', 'l', '-', 'k', 'n', 'o', 'w', 'n',  // Uri-Path: ".well-known"
        0x04, 'c', 'o', 'r', 'e',                                     // Uri-Path: "core"
    };
    udp.beginPacket(ip, 5683);
    udp.write(kGetWellKnownCore, sizeof(kGetWellKnownCore));
    udp.endPacket();

    uint32_t start = millis();
    int packetSize = 0;
    while ((millis() - start) < kReadTimeoutMs) {
        packetSize = udp.parsePacket();
        if (packetSize > 0) break;
        delay(10);
    }
    if (packetSize >= 4) {
        uint8_t reply[160];
        int got = udp.read(reply, (int)sizeof(reply));
        udp.stop();
        if (got >= 4) {
            uint8_t code = reply[1];
            uint8_t codeClass = code >> 5;
            if (codeClass == 2) {
                // Success (2.xx) - find the 0xFF payload marker and take
                // whatever follows as the resource listing, best-effort
                // (not a full CoAP option parser - same "extract, don't
                // fully decode" approach DataStoreProbe uses for JSON).
                String detail = "responds";
                for (int i = 4; i < got - 1; i++) {
                    if (reply[i] == 0xFF) {
                        int payloadLen = got - (i + 1);
                        if (payloadLen > 80) payloadLen = 80;
                        detail = "";
                        for (int k = 0; k < payloadLen; k++) detail += (char)reply[i + 1 + k];
                        break;
                    }
                }
                addFinding(ip, "coap", detail, true);
            } else if (codeClass == 4) {
                addFinding(ip, "coap", "auth/forbidden (4.xx)", false);
            }
        }
    } else {
        udp.stop();
    }
}

void IotOtProbe::probeBacnet(const IPAddress& ip) {
    WiFiUDP udp;
    if (!udp.begin(0)) return;

    // BVLC Original-Unicast-NPDU wrapping an Unconfirmed-Request/Who-Is
    // - the same "who's out there" request every BACnet workstation
    // sends, addressed to one host instead of the whole segment. No
    // device-instance range given, so any device answers its own I-Am
    // regardless of its configured instance number.
    static const uint8_t kWhoIs[] = {
        0x81, 0x0A, 0x00, 0x08,  // BVLC: type=BACnet/IP, function=Original-Unicast-NPDU, length=8
        0x01, 0x00,              // NPDU: version 1, control 0 (no network-layer message)
        0x10, 0x08,              // APDU: Unconfirmed-Request PDU, service choice = Who-Is
    };
    udp.beginPacket(ip, 47808);
    udp.write(kWhoIs, sizeof(kWhoIs));
    udp.endPacket();

    uint32_t start = millis();
    int packetSize = 0;
    while ((millis() - start) < kReadTimeoutMs) {
        packetSize = udp.parsePacket();
        if (packetSize > 0) break;
        delay(10);
    }
    if (packetSize >= 8) {
        uint8_t reply[32];
        int got = udp.read(reply, (int)sizeof(reply));
        udp.stop();
        // Structural check only (not a full BACnet tag-value parser,
        // same "extract just enough to be sure" approach as the rest of
        // this file): BVLC type 0x81, an Unconfirmed-Request PDU (0x10)
        // carrying service choice 0x00 (I-Am) at the expected offset.
        if (got >= 8 && reply[0] == 0x81 && reply[6] == 0x10 && reply[7] == 0x00) {
            addFinding(ip, "bacnet", "responds (I-Am)", true);
        }
    } else {
        udp.stop();
    }
}

void IotOtProbe::probeDnp3(const IPAddress& ip) {
    WiFiClient c;
    if (!c.connect(ip, 20000, kConnectTimeoutMs)) return;

    // Data Link Layer Link Status Request: header-only frame (no
    // application-layer data), broadcast destination (0xFFFF) so it
    // doesn't depend on knowing the outstation's configured address -
    // same technique nmap's dnp3-info script uses. DNP3's link layer has
    // no authentication at all (Secure Authentication is optional and
    // rarely deployed), so any reply carrying the DNP3 sync bytes is
    // itself the finding.
    uint8_t frame[10] = {
        0x05, 0x64,              // sync bytes
        0x05,                    // length: control+dest(2)+src(2) = 5 bytes follow
        0xC9,                    // control: DIR=1(from master) PRM=1 FCB=0 FCV=0 FC=9(REQUEST_LINK_STATUS)
        0xFF, 0xFF,              // destination: 0xFFFF (broadcast)
        0x01, 0x00,              // source: 0x0001 (this probe's arbitrary master address)
        0x00, 0x00,              // CRC, filled in below
    };
    uint16_t crc = dnp3Crc(frame + 2, 6);  // covers length+control+dest+src, not the sync bytes
    frame[8] = (uint8_t)(crc & 0xFF);
    frame[9] = (uint8_t)((crc >> 8) & 0xFF);
    c.write(frame, sizeof(frame));

    uint8_t reply[16] = {0};
    size_t got = readSome(c, kReadTimeoutMs, reply, sizeof(reply));
    // Response content is deliberately NOT validated beyond the sync
    // bytes - see the class doc comment for why (cross-vendor reply-
    // format variance would otherwise cost real findings to false
    // negatives).
    if (got >= 2 && reply[0] == 0x05 && reply[1] == 0x64) {
        addFinding(ip, "dnp3", "responds (link status)", true);
    }
    c.stop();
}

void IotOtProbe::addFinding(const IPAddress& ip, const char* service, const String& detail, bool noAuth) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (_findings.size() < kMaxFindings) _findings.push_back({ip, String(service), detail, noAuth});
        xSemaphoreGive(_mutex);
    }
    notify(String(noAuth ? "OPEN " : "") + service + " @ " + ip.toString());
}

void IotOtProbe::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::IotOt;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

void IotOtProbe::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::IotOt;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}

size_t IotOtProbe::count() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _findings.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool IotOtProbe::get(size_t index, Finding& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _findings.size();
    if (ok) out = _findings[_findings.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
