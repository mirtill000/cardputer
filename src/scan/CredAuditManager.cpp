#include "CredAuditManager.h"
#include "Base64.h"
#include "DefaultCredsDictionary.h"
#include "ScanManager.h"
#include "../core/Config.h"
#include <WiFiClient.h>

CredAuditManager g_credAuditManager;

namespace {

String readStatusLine(WiFiClient& client, uint16_t timeoutMs) {
    String out;
    uint32_t start = millis();
    while ((millis() - start) < timeoutMs) {
        if (client.available()) {
            int c = client.read();
            if (c < 0) break;
            if (c == '\n') break;
            if (c != '\r') out += (char)c;
        } else {
            delay(5);
        }
    }
    return out;
}

// Grabs whatever the peer sends within windowMs — used to scrape a
// telnet login/password prompt and its response. Capped at 200 bytes:
// this is heuristic prompt-scraping, not a transcript.
String readChunk(WiFiClient& client, uint16_t windowMs) {
    String out;
    uint32_t start = millis();
    while ((millis() - start) < windowMs && out.length() < 200) {
        if (client.available()) {
            int c = client.read();
            if (c < 0) break;
            out += (char)c;
        } else {
            delay(10);
        }
    }
    return out;
}

}  // namespace

void CredAuditManager::begin(QueueHandle_t outQueue) {
    _outQueue = outQueue;
}

void CredAuditManager::startAudit(const IPAddress& target) {
    if (_running) return;
    _target = target;
    _running = true;
    notify(ScanEventType::ScanStarted);
    xTaskCreatePinnedToCore(&CredAuditManager::taskEntry, "credaudit", 6144, this, 1, nullptr, 0);
}

void CredAuditManager::taskEntry(void* arg) {
    static_cast<CredAuditManager*>(arg)->run();
    vTaskDelete(nullptr);
}

void CredAuditManager::run() {
    HostInfo h;
    bool found = g_scanManager.getHostByIp(_target, h);

    bool vulnerable = false;
    String note = "no checkable service (run a port scan first — needs an open http or telnet port)";

    if (found) {
        bool hasCheckablePort = false;

        for (const auto& p : h.ports) {
            if (p.service != "http") continue;
            hasCheckablePort = true;
            notify(ScanEventType::ScanProgress, 25);

            String user, pass;
            if (tryHttpBasicAuth(_target, p.port, user, pass)) {
                vulnerable = true;
                note = "http:" + String(p.port) + " accepts " + user + "/" + (pass.length() ? pass : String("<blank>"));
                break;
            }
        }

        if (!vulnerable) {
            for (const auto& p : h.ports) {
                if (p.port != 23) continue;
                hasCheckablePort = true;
                notify(ScanEventType::ScanProgress, 60);

                String user, pass;
                if (tryTelnetLogin(_target, user, pass)) {
                    vulnerable = true;
                    note = "telnet:23 accepts " + user + "/" + (pass.length() ? pass : String("<blank>"));
                }
                break;
            }
        }

        if (hasCheckablePort && !vulnerable) {
            note = "dictionary check found nothing (not proof the device is safe — see README)";
        }
    }

    g_scanManager.setHostCredResult(_target, vulnerable, note);
    _running = false;
    notify(ScanEventType::ScanFinished, 100);
}

bool CredAuditManager::tryHttpBasicAuth(const IPAddress& ip, uint16_t port, String& userOut, String& passOut) {
    for (size_t i = 0; i < DefaultCredsDictionary::kCount; i++) {
        const DefaultCredential& cred = DefaultCredsDictionary::kEntries[i];

        WiFiClient client;
        if (!client.connect(ip, port, g_config.scanTimeoutMs)) return false;  // host stopped responding — bail entirely

        String authB64 = base64::encode(String(cred.user) + ":" + String(cred.pass));

        client.print("GET / HTTP/1.0\r\nHost: scan\r\nAuthorization: Basic ");
        client.print(authB64);
        client.print("\r\nConnection: close\r\n\r\n");

        String statusLine = readStatusLine(client, g_config.scanTimeoutMs);
        client.stop();

        // "HTTP/1.x 200 ..." on a request carrying these credentials
        // means the server accepted them. A 401/403 means it didn't.
        if (statusLine.indexOf(" 200") > 0) {
            userOut = cred.user;
            passOut = cred.pass;
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(g_config.interProbeDelayMs));
    }
    return false;
}

bool CredAuditManager::tryTelnetLogin(const IPAddress& ip, String& userOut, String& passOut) {
    for (size_t i = 0; i < DefaultCredsDictionary::kCount; i++) {
        const DefaultCredential& cred = DefaultCredsDictionary::kEntries[i];

        WiFiClient client;
        if (!client.connect(ip, 23, g_config.scanTimeoutMs)) return false;

        readChunk(client, 400);  // drain the greeting / "login:" prompt before typing anything
        client.print(cred.user);
        client.print("\r\n");

        readChunk(client, 300);  // expect a "Password:" prompt
        client.print(cred.pass);
        client.print("\r\n");

        String response = readChunk(client, 500);
        client.stop();

        // Heuristic, not a protocol-level guarantee — telnetd login
        // prompts vary a lot across implementations (see README). A
        // failure message is strong negative evidence; its absence
        // alongside something that looks like a shell prompt is
        // (weak) positive evidence. Requiring both conditions biases
        // this toward false negatives over false positives on purpose:
        // wrongly clearing a vulnerable device is bad, but wrongly
        // alarming the user about a device that's actually fine erodes
        // trust in every other finding this tool reports.
        String lower = response;
        lower.toLowerCase();
        bool looksFailed = lower.indexOf("incorrect") >= 0 || lower.indexOf("fail") >= 0 ||
                            lower.indexOf("denied") >= 0 || lower.indexOf("invalid") >= 0;
        bool looksLikePrompt = response.indexOf('$') >= 0 || response.indexOf('#') >= 0 ||
                                response.indexOf('>') >= 0;

        if (!looksFailed && looksLikePrompt) {
            userOut = cred.user;
            passOut = cred.pass;
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(g_config.interProbeDelayMs));
    }
    return false;
}

void CredAuditManager::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::CredAudit;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}
