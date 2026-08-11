#include "ResultStore.h"
#include "../core/Types.h"
#include "../scan/ScanManager.h"
#include <cstdio>

namespace {

void writeJsonEscaped(File& f, const String& s) {
    f.print('"');
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '"': f.print("\\\""); break;
            case '\\': f.print("\\\\"); break;
            case '\n': f.print("\\n"); break;
            case '\r': f.print("\\r"); break;
            case '\t': f.print("\\t"); break;
            default:
                if ((uint8_t)c < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)(uint8_t)c);
                    f.print(buf);
                } else {
                    f.print(c);
                }
        }
    }
    f.print('"');
}

const char* riskName(RiskLevel r) {
    switch (r) {
        case RiskLevel::Warning: return "warning";
        case RiskLevel::Critical: return "critical";
        default: return "ok";
    }
}

// Mutates a local copy (doubling embedded quotes per RFC 4180) and
// wraps in quotes only when actually needed, matching how most
// spreadsheet tools write CSV — makes the common case (plain IPs,
// vendor names without commas) readable unquoted.
void csvAppendEscaped(String s, String& out) {
    bool needsQuotes = s.indexOf(',') >= 0 || s.indexOf('"') >= 0 || s.indexOf('\n') >= 0;
    s.replace("\"", "\"\"");
    if (needsQuotes) {
        out += '"';
        out += s;
        out += '"';
    } else {
        out += s;
    }
}

}  // namespace

bool ResultStore::exportJson(fs::FS& fs, const char* path) {
    File f = fs.open(path, "w");
    if (!f) return false;

    f.print('[');
    size_t n = g_scanManager.hostCount();
    bool firstHost = true;
    HostInfo h;

    for (size_t i = 0; i < n; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive) continue;
        if (!firstHost) f.print(',');
        firstHost = false;

        f.print('{');
        f.print("\"ip\":");
        writeJsonEscaped(f, h.ip.toString());
        f.print(",\"mac\":");
        writeJsonEscaped(f, h.macKnown ? macToString(h.mac) : String(""));
        f.print(",\"hostname\":");
        writeJsonEscaped(f, h.hostname);
        f.print(",\"vendor\":");
        writeJsonEscaped(f, h.vendor);
        f.print(",\"class\":");
        writeJsonEscaped(f, String(deviceClassLabel(h.deviceClass)));
        f.print(",\"risk\":");
        writeJsonEscaped(f, String(riskName(h.risk)));
        f.print(",\"credAudited\":");
        f.print(h.credAudited ? "true" : "false");
        f.print(",\"credVulnerable\":");
        f.print(h.credVulnerable ? "true" : "false");
        f.print(",\"credNote\":");
        writeJsonEscaped(f, h.credNote);
        f.print(",\"vulnNote\":");
        writeJsonEscaped(f, h.vulnNote);
        f.print(",\"ports\":[");
        for (size_t p = 0; p < h.ports.size(); p++) {
            if (p) f.print(',');
            const PortResult& pr = h.ports[p];
            f.print('{');
            f.print("\"port\":");
            f.print(pr.port);
            f.print(",\"proto\":");
            writeJsonEscaped(f, pr.isUdp ? String("udp") : String("tcp"));
            f.print(",\"service\":");
            writeJsonEscaped(f, pr.service);
            f.print(",\"banner\":");
            writeJsonEscaped(f, pr.banner);
            f.print(",\"vulnNote\":");
            writeJsonEscaped(f, pr.vulnNote);
            f.print(",\"new\":");
            f.print(pr.isNewPort ? "true" : "false");
            f.print('}');
        }
        f.print("]}");
    }

    f.print(']');
    f.close();
    return true;
}

bool ResultStore::exportCsv(fs::FS& fs, const char* path) {
    File f = fs.open(path, "w");
    if (!f) return false;

    f.println("ip,mac,hostname,vendor,class,risk,cred_audited,cred_vulnerable,cred_note,vuln_note,open_ports");

    size_t n = g_scanManager.hostCount();
    HostInfo h;

    for (size_t i = 0; i < n; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive) continue;

        String openPorts;
        for (size_t p = 0; p < h.ports.size(); p++) {
            if (p) openPorts += ';';
            openPorts += String(h.ports[p].port);
            if (h.ports[p].service.length()) {
                openPorts += '/';
                openPorts += h.ports[p].service;
            }
        }

        String row;
        csvAppendEscaped(h.ip.toString(), row);
        row += ',';
        csvAppendEscaped(h.macKnown ? macToString(h.mac) : String(""), row);
        row += ',';
        csvAppendEscaped(h.hostname, row);
        row += ',';
        csvAppendEscaped(h.vendor, row);
        row += ',';
        csvAppendEscaped(String(deviceClassLabel(h.deviceClass)), row);
        row += ',';
        csvAppendEscaped(String(riskName(h.risk)), row);
        row += ',';
        row += (h.credAudited ? "1" : "0");
        row += ',';
        row += (h.credVulnerable ? "1" : "0");
        row += ',';
        csvAppendEscaped(h.credNote, row);
        row += ',';
        csvAppendEscaped(h.vulnNote, row);
        row += ',';
        csvAppendEscaped(openPorts, row);

        f.println(row);
    }

    f.close();
    return true;
}
