#include "ReportGenerator.h"
#include "../core/Types.h"
#include "../scan/ScanManager.h"
#include "../net/WifiManager.h"
#include "../net/TimeSync.h"
#include <cstdio>

namespace {

// Escapes the five characters that would otherwise break out of HTML
// text/attribute context. Banners and vendor strings come off the wire
// and can contain anything, so every field the report prints goes
// through this first.
void htmlEscape(File& f, const String& s) {
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '&': f.print("&amp;"); break;
            case '<': f.print("&lt;"); break;
            case '>': f.print("&gt;"); break;
            case '"': f.print("&quot;"); break;
            case '\'': f.print("&#39;"); break;
            default: f.print(c);
        }
    }
}

const char* riskName(RiskLevel r) {
    switch (r) {
        case RiskLevel::Warning: return "warning";
        case RiskLevel::Critical: return "critical";
        default: return "ok";
    }
}

const char* riskClass(RiskLevel r) {
    switch (r) {
        case RiskLevel::Warning: return "warn";
        case RiskLevel::Critical: return "crit";
        default: return "ok";
    }
}

bool hostHasService(const HostInfo& h, const char* svc) {
    for (const auto& p : h.ports) {
        if (p.service == svc) return true;
    }
    return false;
}

bool hostHasPort(const HostInfo& h, uint16_t port) {
    for (const auto& p : h.ports) {
        if (p.port == port) return true;
    }
    return false;
}

// The inline stylesheet - dark cyberpunk palette matching the device UI
// (neon green text, cyan/magenta accents), monospace, no external fonts.
void writeStyle(File& f) {
    f.print(
        "<style>"
        "*{box-sizing:border-box}"
        "body{background:#0a0e12;color:#7cfc9e;font-family:'Consolas','Menlo',monospace;"
        "margin:0;padding:24px;font-size:14px;line-height:1.5}"
        "h1{color:#35d6ed;margin:0 0 4px;font-size:22px;letter-spacing:2px}"
        "h2{color:#f81f9e;margin:28px 0 10px;font-size:16px;letter-spacing:1px;"
        "border-bottom:1px solid #223;padding-bottom:4px}"
        ".sub{color:#4a6;margin:0 0 18px;font-size:12px}"
        ".cards{display:flex;flex-wrap:wrap;gap:12px;margin:12px 0}"
        ".card{background:#111820;border:1px solid #223;border-radius:6px;padding:10px 16px;min-width:120px}"
        ".card .n{font-size:24px;color:#35d6ed}"
        ".card .l{font-size:11px;color:#4a6;text-transform:uppercase}"
        "table{border-collapse:collapse;width:100%;margin:8px 0;font-size:12px}"
        "th{background:#111820;color:#f81f9e;text-align:left;padding:6px 8px;border:1px solid #223}"
        "td{padding:5px 8px;border:1px solid #182028;vertical-align:top}"
        "tr:nth-child(even) td{background:#0d1319}"
        ".ok{color:#7cfc9e}.warn{color:#fdc82f}.crit{color:#ff4444}"
        ".tag{display:inline-block;background:#182028;border:1px solid #223;border-radius:3px;"
        "padding:1px 6px;margin:1px;font-size:11px;color:#9cf}"
        ".finding{background:#111820;border-left:3px solid #ff4444;padding:8px 12px;margin:8px 0}"
        ".finding.warn{border-left-color:#fdc82f}"
        ".finding .who{color:#35d6ed}"
        ".muted{color:#456}"
        "footer{margin-top:32px;color:#456;font-size:11px;border-top:1px solid #223;padding-top:12px}"
        "code{color:#9cf}"
        "</style>");
}

}  // namespace

bool ReportGenerator::generate(fs::FS& fs, const char* path) {
    File f = fs.open(path, "w");
    if (!f) return false;

    // --- Gather summary counts and note the standout findings in one
    // pass, then stream them out. Counts are small ints; the findings are
    // emitted inline below with a second walk over the table. ---
    size_t n = g_scanManager.hostCount();
    HostInfo h;
    uint32_t aliveCount = 0, warnCount = 0, critCount = 0, credVulnCount = 0, withPortsCount = 0;
    for (size_t i = 0; i < n; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive) continue;
        aliveCount++;
        if (h.risk == RiskLevel::Warning) warnCount++;
        if (h.risk == RiskLevel::Critical) critCount++;
        if (h.credVulnerable) credVulnCount++;
        if (!h.ports.empty()) withPortsCount++;
    }

    String ts = TimeSync::nowString();
    if (ts.isEmpty()) ts = "clock not synced (uptime " + String(millis() / 1000) + "s)";

    f.print("<!doctype html><html><head><meta charset=\"utf-8\">");
    f.print("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
    f.print("<title>NETRUNNER Assessment Report</title>");
    writeStyle(f);
    f.print("</head><body>");

    f.print("<h1>NETRUNNER // ASSESSMENT REPORT</h1>");
    f.print("<p class=\"sub\">network: <code>");
    htmlEscape(f, g_wifi.currentSsid().length() ? g_wifi.currentSsid() : String("(offline)"));
    f.print("</code> &nbsp; subnet: <code>");
    htmlEscape(f, g_wifi.networkAddress().toString());
    f.print("</code> &nbsp; gateway: <code>");
    htmlEscape(f, g_wifi.gatewayIP().toString());
    f.print("</code><br>generated: ");
    htmlEscape(f, ts);
    f.print("</p>");

    // --- Summary cards ---
    f.print("<div class=\"cards\">");
    auto card = [&](uint32_t v, const char* label) {
        f.print("<div class=\"card\"><div class=\"n\">");
        f.print(v);
        f.print("</div><div class=\"l\">");
        f.print(label);
        f.print("</div></div>");
    };
    card(aliveCount, "hosts");
    card(withPortsCount, "with open ports");
    card(warnCount, "warning");
    card(critCount, "critical");
    card(credVulnCount, "cred-vulnerable");
    f.print("</div>");

    // --- Attack surface / kill chain: the interesting stuff first. A
    // host can raise several findings; each is one line, most-severe
    // category first (creds -> plaintext svc -> known-vuln banner ->
    // exposed SMB). Purely descriptive - it points at what an attacker
    // would look at, it doesn't do anything. ---
    f.print("<h2>ATTACK SURFACE</h2>");
    bool anyFinding = false;

    for (size_t i = 0; i < n; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive) continue;
        if (!h.credVulnerable) continue;
        anyFinding = true;
        f.print("<div class=\"finding\"><span class=\"who\">");
        htmlEscape(f, h.ip.toString());
        f.print("</span> &mdash; default/weak credentials accepted");
        if (h.credNote.length()) { f.print(": "); htmlEscape(f, h.credNote); }
        f.print("</div>");
    }
    for (size_t i = 0; i < n; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive) continue;
        bool telnet = hostHasService(h, "telnet") || hostHasPort(h, 23);
        bool ftp = hostHasService(h, "ftp") || hostHasPort(h, 21);
        if (!telnet && !ftp) continue;
        anyFinding = true;
        f.print("<div class=\"finding warn\"><span class=\"who\">");
        htmlEscape(f, h.ip.toString());
        f.print("</span> &mdash; plaintext service exposed (");
        if (telnet) f.print("telnet");
        if (telnet && ftp) f.print(", ");
        if (ftp) f.print("ftp");
        f.print(") &mdash; credentials travel unencrypted</div>");
    }
    for (size_t i = 0; i < n; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive) continue;
        if (!h.vulnNote.length()) continue;
        anyFinding = true;
        f.print("<div class=\"finding\"><span class=\"who\">");
        htmlEscape(f, h.ip.toString());
        f.print("</span> &mdash; banner matched known-vulnerable signature: ");
        htmlEscape(f, h.vulnNote);
        f.print("</div>");
    }
    for (size_t i = 0; i < n; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive) continue;
        bool smb = hostHasService(h, "smb") || hostHasService(h, "netbios-ssn") ||
                   hostHasPort(h, 445) || hostHasPort(h, 139);
        if (!smb) continue;
        anyFinding = true;
        f.print("<div class=\"finding warn\"><span class=\"who\">");
        htmlEscape(f, h.ip.toString());
        f.print("</span> &mdash; SMB/NetBIOS exposed &mdash; check share ACLs and signing "
                "(on-device: host detail &rarr; S)</div>");
    }

    if (!anyFinding) {
        f.print("<p class=\"muted\">No standout findings flagged. This does not mean the "
                "network is secure &mdash; it means nothing matched the heuristics above. "
                "Run port scans and credential audits on individual hosts for depth.</p>");
    }

    // --- Full inventory table ---
    f.print("<h2>HOST INVENTORY</h2>");
    f.print("<table><tr><th>IP</th><th>MAC</th><th>Hostname</th><th>Vendor</th>"
            "<th>Class</th><th>Risk</th><th>Open ports</th></tr>");
    for (size_t i = 0; i < n; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive) continue;
        f.print("<tr><td>");
        htmlEscape(f, h.ip.toString());
        f.print("</td><td>");
        htmlEscape(f, h.macKnown ? macToString(h.mac) : String("-"));
        f.print("</td><td>");
        htmlEscape(f, h.hostname.length() ? h.hostname : String("-"));
        f.print("</td><td>");
        htmlEscape(f, h.vendor.length() ? h.vendor : String("-"));
        f.print("</td><td>");
        htmlEscape(f, String(deviceClassLabel(h.deviceClass)));
        f.print("</td><td class=\"");
        f.print(riskClass(h.risk));
        f.print("\">");
        f.print(riskName(h.risk));
        f.print("</td><td>");
        if (h.ports.empty()) {
            f.print("<span class=\"muted\">not scanned</span>");
        } else {
            for (const auto& p : h.ports) {
                f.print("<span class=\"tag\">");
                f.print(p.port);
                if (p.service.length()) { f.print("/"); htmlEscape(f, p.service); }
                f.print("</span>");
            }
        }
        f.print("</td></tr>");
    }
    f.print("</table>");

    // --- Companion artifacts actually present on this card ---
    f.print("<h2>COMPANION ARTIFACTS</h2>");
    // wardrive.csv moved under /netrunner/ in Fase 29 - covered by the
    // generic /netrunner/ directory check below now, not a fixed path
    // here anymore. The two fixed-path CSVs here are the ones offensive
    // sessions live-append outside /netrunner/: evil-twin associations
    // and MITM cleartext harvest (the latter carries real captured
    // credentials/cookies - flagged so the report indexes it rather than
    // leaving it invisible).
    struct FixedArtifact {
        const char* path;
        const char* note;  // nullptr = list the path alone
    };
    const FixedArtifact artifacts[] = {
        {"/eviltwin/associations.csv", "clients that joined a look-alike AP (MAC + timestamp)"},
        {"/mitm/harvest.csv", "MITM AUDIT: cleartext credentials/cookies captured - handle as sensitive"},
    };
    bool anyArtifact = false;
    f.print("<ul>");
    for (const FixedArtifact& a : artifacts) {
        if (fs.exists(a.path)) {
            anyArtifact = true;
            f.print("<li><code>");
            f.print(a.path);
            f.print("</code>");
            if (a.note) {
                f.print(" &mdash; ");
                f.print(a.note);
            }
            f.print("</li>");
        }
    }
    // /netrunner/ - every NETWORK SCAN export (JSON/CSV) and HTML report
    // (this one included, and every earlier one), one timestamped file
    // per run (see storage/NetrunnerPaths.h), plus WardrivingManager's
    // continuous wardrive.csv sighting log and its own per-AP excursion
    // exports (Fase 29). Listed as a directory, not individual
    // filenames, same as /handshakes/ below: the exact filename varies
    // per run/AP, unlike the fixed-name artifacts above.
    if (fs.exists("/netrunner")) {
        anyArtifact = true;
        f.print(
            "<li><code>/netrunner/</code> &mdash; every scan export/report and the war-driving log, one file per "
            "run</li>");
    }
    if (fs.exists("/handshakes")) {
        anyArtifact = true;
        f.print("<li><code>/handshakes/</code> &mdash; captured WPA/PMKID material (.pcap, crack offline)</li>");
    }
    if (!anyArtifact) f.print("<li class=\"muted\">none found on this card yet</li>");
    f.print("</ul>");

    f.print("<footer>Generated by NETRUNNER on M5Stack Cardputer. For authorized assessment "
            "of networks you own or have explicit written permission to test. This report "
            "describes observed exposure; it takes no action against any host.</footer>");
    f.print("</body></html>");

    f.close();
    return true;
}
