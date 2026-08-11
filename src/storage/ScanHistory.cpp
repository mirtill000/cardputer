#include "ScanHistory.h"
#include "../net/TimeSync.h"
#include "../net/WifiManager.h"
#include "../scan/ScanManager.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <cstdio>

namespace {

constexpr const char* kNvsNamespace = "history";

const char* riskName(RiskLevel r) {
    switch (r) {
        case RiskLevel::Warning: return "warning";
        case RiskLevel::Critical: return "critical";
        default: return "ok";
    }
}

// Monotonically increasing across reboots (there's no RTC/NTP on this
// board - see README - so a sequence number is the only reliable way to
// order snapshots and to know which file is "the previous one").
uint32_t nextSeq() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) return 0;
    uint32_t seq = prefs.getUInt("seq", 0) + 1;
    prefs.putUInt("seq", seq);
    prefs.end();
    return seq;
}

uint32_t currentSeq() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) return 0;
    uint32_t seq = prefs.getUInt("seq", 0);
    prefs.end();
    return seq;
}

String scanPathFor(uint32_t seq) {
    char buf[40];
    snprintf(buf, sizeof(buf), "/history/scan_%05u.json", (unsigned)seq);
    return String(buf);
}

String portsPathFor(const IPAddress& ip) {
    String s = ip.toString();
    s.replace('.', '_');
    return "/history/ports_" + s + ".json";
}

}  // namespace

bool ScanHistory::saveSnapshot(fs::FS& fs) {
    fs.mkdir("/history");

    uint32_t seq = nextSeq();
    if (seq == 0) return false;  // NVS unavailable

    File f = fs.open(scanPathFor(seq), "w");
    if (!f) return false;

    JsonDocument doc;
    doc["seq"] = seq;
    // Best-effort: "" if NTP hasn't synced yet (no WiFi, or too soon
    // after connecting) - seq is still the reliable ordering key either
    // way, this is purely for human-readable display.
    doc["time"] = TimeSync::nowString();
    // "" if not connected (shouldn't normally happen - a discovery scan
    // needs a connection to run at all - but g_wifi.currentSsid() is
    // cheap to call regardless).
    doc["network"] = g_wifi.currentSsid();
    JsonArray hosts = doc["hosts"].to<JsonArray>();

    size_t n = g_scanManager.hostCount();
    HostInfo h;
    for (size_t i = 0; i < n; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive) continue;
        JsonObject o = hosts.add<JsonObject>();
        o["ip"] = h.ip.toString();
        o["mac"] = h.macKnown ? macToString(h.mac) : String("");
        o["hostname"] = h.hostname;
        o["vendor"] = h.vendor;
        o["class"] = deviceClassLabel(h.deviceClass);
        o["risk"] = riskName(h.risk);
    }

    serializeJson(doc, f);
    f.close();

    // Prune anything older than the kMaxEntries-wide window that ends
    // at this seq - saveSnapshot() is the only thing that ever creates
    // scan_*.json files, and it always does so with consecutive seq
    // numbers, so "older than the window" is just "seq below the
    // window's floor", nothing to list or sort.
    if (seq > kMaxEntries) {
        uint32_t pruneSeq = seq - kMaxEntries;
        fs.remove(scanPathFor(pruneSeq));
    }

    return true;
}

size_t ScanHistory::listEntries(fs::FS& fs, std::vector<HistoryEntry>& out) {
    out.clear();
    uint32_t maxSeq = currentSeq();
    if (maxSeq == 0) return 0;
    uint32_t minSeq = (maxSeq > kMaxEntries) ? (maxSeq - kMaxEntries + 1) : 1;

    uint32_t seq = maxSeq;
    while (true) {
        String path = scanPathFor(seq);
        File f = fs.open(path, "r");
        if (f) {
            JsonDocument doc;
            if (deserializeJson(doc, f) == DeserializationError::Ok) {
                HistoryEntry e;
                e.filename = path;
                e.seq = doc["seq"] | seq;
                e.hostCount = doc["hosts"].as<JsonArray>().size();
                e.time = doc["time"] | "";
                e.network = doc["network"] | "";
                out.push_back(e);
            }
            f.close();
        }
        if (seq <= minSeq) break;
        seq--;
    }
    return out.size();
}

bool ScanHistory::loadEntry(fs::FS& fs, const String& filename, std::vector<HistoryHostSnapshot>& out) {
    File f = fs.open(filename, "r");
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;

    out.clear();
    for (JsonObject o : doc["hosts"].as<JsonArray>()) {
        HistoryHostSnapshot s;
        String ipStr = o["ip"] | "";
        s.ip.fromString(ipStr);
        s.mac = o["mac"] | "";
        s.hostname = o["hostname"] | "";
        s.vendor = o["vendor"] | "";
        s.deviceClass = o["class"] | "";
        s.risk = o["risk"] | "";
        out.push_back(s);
    }
    return true;
}

size_t ScanHistory::diffNewHosts(fs::FS& fs, std::vector<IPAddress>& newHostsOut) {
    newHostsOut.clear();

    std::vector<HistoryEntry> entries;
    listEntries(fs, entries);
    if (entries.size() < 2) return 0;  // nothing to compare the just-saved scan against yet

    std::vector<HistoryHostSnapshot> previous;
    if (!loadEntry(fs, entries[1].filename, previous)) return 0;

    size_t n = g_scanManager.hostCount();
    HostInfo h;
    for (size_t i = 0; i < n; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive) continue;
        bool foundInPrevious = false;
        for (const auto& p : previous) {
            if (p.ip == h.ip) {
                foundInPrevious = true;
                break;
            }
        }
        if (!foundInPrevious) newHostsOut.push_back(h.ip);
    }
    return newHostsOut.size();
}

size_t ScanHistory::loadKnownMacs(fs::FS& fs, const String& network, std::vector<String>& macsOut) {
    macsOut.clear();
    if (network.isEmpty()) return 0;  // "" never matches a real SSID - see saveSnapshot()

    std::vector<HistoryEntry> entries;
    listEntries(fs, entries);

    for (const auto& e : entries) {
        if (e.network != network) continue;
        std::vector<HistoryHostSnapshot> hosts;
        if (!loadEntry(fs, e.filename, hosts)) continue;
        for (const auto& h : hosts) {
            if (h.mac.length()) macsOut.push_back(h.mac);
        }
    }
    return macsOut.size();
}

void ScanHistory::diffAndSavePorts(fs::FS& fs, const IPAddress& target, std::vector<PortResult>& ports) {
    fs.mkdir("/history");
    String path = portsPathFor(target);

    bool hasBaseline = false;
    std::vector<uint16_t> previousPorts;
    File rf = fs.open(path, "r");
    if (rf) {
        hasBaseline = true;
        JsonDocument doc;
        if (deserializeJson(doc, rf) == DeserializationError::Ok) {
            for (JsonVariant v : doc["ports"].as<JsonArray>()) previousPorts.push_back(v.as<uint16_t>());
        }
        rf.close();
    }

    for (auto& p : ports) {
        bool wasOpen = false;
        for (uint16_t prevPort : previousPorts) {
            if (prevPort == p.port) {
                wasOpen = true;
                break;
            }
        }
        p.isNewPort = hasBaseline && !wasOpen;
    }

    JsonDocument out;
    JsonArray arr = out["ports"].to<JsonArray>();
    for (const auto& p : ports) arr.add(p.port);

    File wf = fs.open(path, "w");
    if (wf) {
        serializeJson(out, wf);
        wf.close();
    }
}
