#include "DataStoreProbe.h"
#include "ScanManager.h"
#include "../core/Types.h"
#include <WiFiClient.h>
#include <cstring>

DataStoreProbe g_dataStoreProbe;

namespace {

// Reads up to maxLen bytes for up to timeoutMs, returning whatever arrived
// (stops early once the peer pauses after sending something).
String readSome(WiFiClient& c, uint16_t timeoutMs, size_t maxLen) {
    String out;
    uint32_t start = millis();
    while ((millis() - start) < timeoutMs && out.length() < maxLen) {
        if (c.available()) {
            out += (char)c.read();
        } else if (out.length() > 0) {
            break;
        } else {
            delay(5);
        }
    }
    return out;
}

// Pulls "field:value" out of a Redis INFO / text blob.
String extractField(const String& blob, const char* field) {
    int p = blob.indexOf(field);
    if (p < 0) return "";
    p += strlen(field);
    int end = blob.indexOf('\r', p);
    if (end < 0) end = blob.indexOf('\n', p);
    if (end < 0) end = blob.length();
    String v = blob.substring(p, end);
    v.trim();
    return v;
}

}  // namespace

void DataStoreProbe::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

bool DataStoreProbe::start() {
    if (_running) {
        notify("datastore sweep already running");
        return false;
    }
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _findings.clear();
        xSemaphoreGive(_mutex);
    }
    _progressPct = 0;
    _running = true;
    notify(ScanEventType::ScanStarted);
    if (xTaskCreatePinnedToCore(&DataStoreProbe::taskEntry, "datastore", 6144, this, 1, nullptr, 0) != pdPASS) {
        // Task never started (out of memory) - clear the running flag.
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return false;
    }
    return true;
}

void DataStoreProbe::taskEntry(void* arg) {
    static_cast<DataStoreProbe*>(arg)->run();
    vTaskDelete(nullptr);
}

void DataStoreProbe::run() {
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
        notify("probing " + targets[i].toString());
        probeHost(targets[i]);
        _progressPct = (uint8_t)(((i + 1) * 100) / targets.size());
        notify(ScanEventType::ScanProgress, _progressPct);
    }

    notify(String((unsigned)count()) + " exposed store(s)");
    _running = false;
    notify(ScanEventType::ScanFinished, 100);
}

void DataStoreProbe::probeHost(const IPAddress& ip) {
    // --- Redis (6379) ---
    {
        WiFiClient c;
        if (c.connect(ip, 6379, kConnectTimeoutMs)) {
            c.print("PING\r\n");
            String r = readSome(c, kReadTimeoutMs, 64);
            if (r.startsWith("+PONG")) {
                c.print("INFO server\r\n");
                String info = readSome(c, kReadTimeoutMs, 512);
                String ver = extractField(info, "redis_version:");
                addFinding(ip, "redis", ver.length() ? ver : String("no-auth"), true);
            } else if (r.startsWith("-NOAUTH") || r.indexOf("NOAUTH") >= 0) {
                addFinding(ip, "redis", "auth required", false);
            }
            c.stop();
        }
    }
    // --- Memcached (11211) ---
    {
        WiFiClient c;
        if (c.connect(ip, 11211, kConnectTimeoutMs)) {
            c.print("version\r\n");
            String r = readSome(c, kReadTimeoutMs, 64);
            if (r.startsWith("VERSION")) {
                String ver = r.substring(8);
                ver.trim();
                addFinding(ip, "memcached", ver.length() ? ver : String("no-auth"), true);
            }
            c.stop();
        }
    }
    // --- Elasticsearch (9200, HTTP) ---
    {
        WiFiClient c;
        if (c.connect(ip, 9200, kConnectTimeoutMs)) {
            c.print("GET / HTTP/1.0\r\nHost: es\r\nConnection: close\r\n\r\n");
            String r = readSome(c, kReadTimeoutMs, 768);
            int sp = r.indexOf(' ');
            int code = (sp > 0) ? r.substring(sp + 1, sp + 4).toInt() : 0;
            if (code == 200) {
                String ver = extractField(r, "\"number\" : \"");  // ES pretty-prints with spaces
                if (!ver.length()) ver = extractField(r, "\"number\":\"");
                int q = ver.indexOf('"');
                if (q > 0) ver = ver.substring(0, q);
                addFinding(ip, "elasticsearch", ver.length() ? ver : String("no-auth"), true);
            } else if (code == 401) {
                addFinding(ip, "elasticsearch", "secured (401)", false);
            }
            c.stop();
        }
    }
    // --- MongoDB (27017) ---
    {
        WiFiClient c;
        if (c.connect(ip, 27017, kConnectTimeoutMs)) {
            // OP_QUERY on admin.$cmd for {listDatabases:1}. If the reply
            // carries a "databases" array it answered a privileged command
            // with no auth; an auth-error string means it's locked down.
            auto buildBson = [](const char* key, std::vector<uint8_t>& out) {
                size_t klen = strlen(key);
                uint32_t docLen = 4 + 1 + (klen + 1) + 4 + 1;
                out.push_back(docLen & 0xFF);
                out.push_back((docLen >> 8) & 0xFF);
                out.push_back((docLen >> 16) & 0xFF);
                out.push_back((docLen >> 24) & 0xFF);
                out.push_back(0x10);  // int32 element
                for (size_t k = 0; k < klen; k++) out.push_back((uint8_t)key[k]);
                out.push_back(0x00);
                out.push_back(0x01);  // value = 1
                out.push_back(0x00);
                out.push_back(0x00);
                out.push_back(0x00);
                out.push_back(0x00);  // doc terminator
            };
            std::vector<uint8_t> bson;
            buildBson("listDatabases", bson);

            const char* coll = "admin.$cmd";
            size_t collLen = strlen(coll) + 1;
            uint32_t bodyLen = 4 + collLen + 4 + 4 + bson.size();  // flags+coll+skip+return+query
            uint32_t msgLen = 16 + bodyLen;

            std::vector<uint8_t> msg;
            auto put32 = [&](uint32_t v) {
                msg.push_back(v & 0xFF);
                msg.push_back((v >> 8) & 0xFF);
                msg.push_back((v >> 16) & 0xFF);
                msg.push_back((v >> 24) & 0xFF);
            };
            put32(msgLen);
            put32(1);      // requestID
            put32(0);      // responseTo
            put32(2004);   // OP_QUERY
            put32(0);      // flags
            for (size_t k = 0; k < collLen; k++) msg.push_back((uint8_t)coll[k]);
            put32(0);      // numberToSkip
            put32(1);      // numberToReturn
            msg.insert(msg.end(), bson.begin(), bson.end());

            c.write(msg.data(), msg.size());
            String reply = readSome(c, kReadTimeoutMs, 900);
            if (reply.length() > 16) {  // got an OP_REPLY of some kind
                bool authErr = reply.indexOf("not authorized") >= 0 || reply.indexOf("requires auth") >= 0 ||
                               reply.indexOf("Authentication") >= 0 || reply.indexOf("Unauthorized") >= 0;
                bool listed = reply.indexOf("databases") >= 0 && !authErr;
                addFinding(ip, "mongodb", listed ? "no-auth: listDatabases OK" : "reachable (auth required?)",
                           listed);
            }
            c.stop();
        }
    }
}

void DataStoreProbe::addFinding(const IPAddress& ip, const char* store, const String& detail, bool noAuth) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (_findings.size() < kMaxFindings) _findings.push_back({ip, String(store), detail, noAuth});
        xSemaphoreGive(_mutex);
    }
    notify(String(noAuth ? "OPEN " : "") + store + " @ " + ip.toString());
}

void DataStoreProbe::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::DataStore;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

void DataStoreProbe::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::DataStore;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}

size_t DataStoreProbe::count() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _findings.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool DataStoreProbe::get(size_t index, Finding& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _findings.size();
    if (ok) out = _findings[_findings.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
