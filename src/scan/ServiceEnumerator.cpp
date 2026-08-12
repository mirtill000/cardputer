#include "ServiceEnumerator.h"
#include "../net/DnsWire.h"
#include <WiFiUdp.h>

ServiceEnumerator g_serviceEnumerator;

namespace {
constexpr uint16_t kMdnsPort = 5353;
const IPAddress kMdnsMcast(224, 0, 0, 251);

// DNS record types we care about.
constexpr uint16_t kTypePtr = 12;
constexpr uint16_t kTypeSrv = 33;

// Trims a trailing ".local" for compact display.
String trimLocal(const String& s) {
    if (s.endsWith(".local")) return s.substring(0, s.length() - 6);
    return s;
}

// The DNS-SD instance name is "<instance>._<svc>._tcp.local"; the human
// label is everything before the "._" that starts the service type.
String instanceLabel(const String& fullName) {
    int p = fullName.indexOf("._");
    return (p > 0) ? fullName.substring(0, p) : trimLocal(fullName);
}

// Sends one mDNS query for `name`/`qtype` and, for windowMs, invokes
// cb(owner, rtype, buf, len, rdataPos, rdlen) on every resource record in
// every response packet. Reuses the DnsWire name helpers (skipName /
// decodeName) so the compression handling lives in one verified place.
template <typename CB>
void queryAndWalk(WiFiUDP& udp, const String& name, uint16_t qtype, uint32_t windowMs, CB cb) {
    std::vector<uint8_t> pkt = dnswire::buildQuery(name, qtype);
    udp.beginPacket(kMdnsMcast, kMdnsPort);
    udp.write(pkt.data(), pkt.size());
    udp.endPacket();

    uint8_t buf[1280];
    uint32_t start = millis();
    while ((millis() - start) < windowMs) {
        int size = udp.parsePacket();
        if (size <= 0) {
            delay(10);
            continue;
        }
        int cap = (size > (int)sizeof(buf)) ? (int)sizeof(buf) : size;
        int len = udp.read(buf, cap);
        if (len < 12) continue;

        uint16_t qd = ((uint16_t)buf[4] << 8) | buf[5];
        uint32_t total = (uint32_t)(((uint16_t)buf[6] << 8) | buf[7]) +     // ancount
                         (((uint16_t)buf[8] << 8) | buf[9]) +               // nscount
                         (((uint16_t)buf[10] << 8) | buf[11]);              // arcount

        int pos = 12;
        bool ok = true;
        for (uint16_t i = 0; i < qd && ok; i++) {
            pos = dnswire::skipName(buf, len, pos);
            if (pos < 0 || pos + 4 > len) ok = false;
            else pos += 4;
        }
        for (uint32_t r = 0; r < total && ok; r++) {
            String owner = dnswire::decodeName(buf, len, pos);
            pos = dnswire::skipName(buf, len, pos);
            if (pos < 0 || pos + 10 > len) break;
            uint16_t rtype = ((uint16_t)buf[pos] << 8) | buf[pos + 1];
            uint16_t rdlen = ((uint16_t)buf[pos + 8] << 8) | buf[pos + 9];
            pos += 10;
            if (pos + rdlen > len) break;
            cb(owner, rtype, buf, len, pos, rdlen);
            pos += rdlen;
        }
    }
}

}  // namespace

void ServiceEnumerator::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

bool ServiceEnumerator::start() {
    if (_running) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _services.clear();
        xSemaphoreGive(_mutex);
    }
    _running = true;
    notify(ScanEventType::ScanStarted);
    xTaskCreatePinnedToCore(&ServiceEnumerator::taskEntry, "svcenum", 8192, this, 1, nullptr, 0);
    return true;
}

void ServiceEnumerator::taskEntry(void* arg) {
    static_cast<ServiceEnumerator*>(arg)->run();
    vTaskDelete(nullptr);
}

void ServiceEnumerator::run() {
    WiFiUDP udp;
    // Same 2-arg beginMulticast(mcast, port) as MdnsReverseResolver — see
    // its RISK note: if the core wants the 3-arg (interfaceAddr, mcast,
    // port) form, pass WiFi.localIP() as the first argument.
    if (!udp.beginMulticast(kMdnsMcast, kMdnsPort)) {
        notify("failed to open mDNS socket");
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return;
    }

    // Phase 1: enumerate service types.
    std::vector<String> types;
    queryAndWalk(udp, "_services._dns-sd._udp.local", kTypePtr, kTypeWindowMs,
                 [&](const String& owner, uint16_t rtype, const uint8_t* buf, int len, int rdpos, uint16_t rdlen) {
                     (void)owner;
                     (void)rdlen;
                     if (rtype != kTypePtr) return;
                     String svc = dnswire::decodeName(buf, len, rdpos);
                     if (!svc.length()) return;
                     for (const auto& t : types)
                         if (t == svc) return;  // dedup
                     if (types.size() < kMaxTypes) types.push_back(svc);
                 });

    notify(String((unsigned)types.size()) + " service type(s), querying...");

    // Phase 2: enumerate instances per type. Instances are collected into
    // a local list first so SRV ports (which arrive as separate records in
    // the same response) can be matched back to their instance by name
    // before anything is published.
    for (size_t ti = 0; ti < types.size() && _running; ti++) {
        const String& type = types[ti];
        std::vector<String> fullNames;   // parallel to `local`, for SRV matching
        std::vector<Service> local;

        queryAndWalk(udp, type, kTypePtr, kInstanceWindowMs,
                     [&](const String& owner, uint16_t rtype, const uint8_t* buf, int len, int rdpos, uint16_t rdlen) {
                         if (rtype == kTypePtr) {
                             String inst = dnswire::decodeName(buf, len, rdpos);
                             if (!inst.length()) return;
                             for (const auto& fn : fullNames)
                                 if (fn == inst) return;  // dedup within this type
                             Service s;
                             s.type = trimLocal(type);
                             s.instance = instanceLabel(inst);
                             s.port = 0;
                             fullNames.push_back(inst);
                             local.push_back(s);
                         } else if (rtype == kTypeSrv && rdlen >= 6) {
                             uint16_t port = ((uint16_t)buf[rdpos + 4] << 8) | buf[rdpos + 5];
                             for (size_t k = 0; k < fullNames.size(); k++) {
                                 if (fullNames[k] == owner) {
                                     local[k].port = port;
                                     break;
                                 }
                             }
                         }
                     });

        for (const auto& s : local) addService(s);
    }

    notify(String((unsigned)count()) + " service(s) found");
    _running = false;
    notify(ScanEventType::ScanFinished, 100);
}

void ServiceEnumerator::addService(const Service& s) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        bool dup = false;
        for (const auto& e : _services) {
            if (e.type == s.type && e.instance == s.instance) {
                dup = true;
                break;
            }
        }
        if (!dup && _services.size() < kMaxServices) _services.push_back(s);
        xSemaphoreGive(_mutex);
    }
}

void ServiceEnumerator::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::ServiceEnum;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

void ServiceEnumerator::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::ServiceEnum;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}

size_t ServiceEnumerator::count() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _services.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool ServiceEnumerator::get(size_t index, Service& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _services.size();
    if (ok) out = _services[_services.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
