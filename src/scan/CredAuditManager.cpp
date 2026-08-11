#include "CredAuditManager.h"
#include "Base64.h"
#include "DefaultCredsDictionary.h"
#include "ScanManager.h"
#include "WordlistLoader.h"
#include "../core/Config.h"
#include "../core/Types.h"
#include "../ui/Sound.h"
#include <WiFiClient.h>
#include <cstring>

CredAuditManager g_credAuditManager;

namespace {

constexpr size_t kMaxWordlistEntries = 200;  // bounds RAM + worst-case run time — see WordlistLoader.h

String readLine(WiFiClient& client, uint16_t timeoutMs) {
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

void CredAuditManager::ensureWordlistsLoaded() {
    if (_wordlistsLoaded) return;
    _users = WordlistLoader::load("/creds/users.txt", kMaxWordlistEntries);
    _passwords = WordlistLoader::load("/creds/passwords.txt", kMaxWordlistEntries);
    _wordlistsLoaded = true;
}

void CredAuditManager::startAudit(const IPAddress& target) {
    if (_running) return;
    _target = target;
    _attempts = 0;
    _successes = 0;
    _running = true;
    notify(ScanEventType::ScanStarted);
    xTaskCreatePinnedToCore(&CredAuditManager::taskEntry, "credaudit", 6144, this, 1, nullptr, 0);
}

void CredAuditManager::taskEntry(void* arg) {
    static_cast<CredAuditManager*>(arg)->run();
    vTaskDelete(nullptr);
}

void CredAuditManager::run() {
    ensureWordlistsLoaded();

    HostInfo h;
    bool found = g_scanManager.getHostByIp(_target, h);

    bool vulnerable = false;
    String note = "no checkable service (run a port scan first - needs an open http, telnet or ftp port)";
    String hitUser, hitPass, hitService;
    uint16_t hitPort = 0;

    if (found) {
        bool triedAny = false;

        for (const auto& p : h.ports) {
            const char* service = nullptr;
            if (p.service == "http") service = "http";
            else if (p.service == "telnet") service = "telnet";
            else if (p.service == "ftp") service = "ftp";
            if (!service) continue;

            triedAny = true;
            String user, pass;
            if (attemptService(service, p.port, user, pass)) {
                vulnerable = true;
                hitUser = user;
                hitPass = pass;
                hitService = service;
                hitPort = p.port;
                break;
            }
        }

        if (vulnerable) {
            note = hitService + ":" + String(hitPort) + " accepts " + hitUser + "/" +
                   (hitPass.length() ? hitPass : String("<blank>"));
        } else if (triedAny) {
            note = String(_attempts) + " combinations tried, nothing accepted (not proof the device is safe)";
        }
    }

    if (vulnerable) sound::playCredAlert();

    g_scanManager.setHostCredResult(_target, vulnerable, note);
    _running = false;
    notify(ScanEventType::ScanFinished, 100);
}

bool CredAuditManager::attemptService(const char* serviceName, uint16_t port, String& outUser, String& outPass) {
    auto tryOne = [&](const String& user, const String& pass) -> bool {
        bool ok;
        if (strcmp(serviceName, "http") == 0) {
            ok = tryHttpBasicAuth(_target, port, user, pass);
        } else if (strcmp(serviceName, "telnet") == 0) {
            ok = tryTelnetLogin(_target, user, pass);
        } else {
            ok = tryFtpLogin(_target, user, pass);
        }
        _attempts++;
        if (ok) _successes++;
        logAttempt(serviceName, user, pass, ok);
        return ok;
    };

    // Quick pass: the small, well-known built-in dictionary first (fast
    // hits on the common case) before the slower full wordlist sweep.
    for (size_t i = 0; i < DefaultCredsDictionary::kCount; i++) {
        const DefaultCredential& cred = DefaultCredsDictionary::kEntries[i];
        if (tryOne(cred.user, cred.pass)) {
            outUser = cred.user;
            outPass = cred.pass;
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(g_config.interProbeDelayMs));
    }

    for (const auto& user : _users) {
        for (const auto& pass : _passwords) {
            if (tryOne(user, pass)) {
                outUser = user;
                outPass = pass;
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(g_config.interProbeDelayMs));
        }
    }

    return false;
}

bool CredAuditManager::tryHttpBasicAuth(const IPAddress& ip, uint16_t port, const String& user, const String& pass) {
    WiFiClient client;
    if (!client.connect(ip, port, g_config.scanTimeoutMs)) return false;

    String authB64 = b64::encode(user + ":" + pass);
    client.print("GET / HTTP/1.0\r\nHost: scan\r\nAuthorization: Basic ");
    client.print(authB64);
    client.print("\r\nConnection: close\r\n\r\n");

    String statusLine = readLine(client, g_config.scanTimeoutMs);
    client.stop();

    // "HTTP/1.x 200 ..." on a request carrying these credentials means
    // the server accepted them. A 401/403 means it didn't.
    return statusLine.indexOf(" 200") > 0;
}

bool CredAuditManager::tryTelnetLogin(const IPAddress& ip, const String& user, const String& pass) {
    WiFiClient client;
    if (!client.connect(ip, 23, g_config.scanTimeoutMs)) return false;

    readChunk(client, 400);  // drain the greeting / "login:" prompt before typing anything
    client.print(user);
    client.print("\r\n");

    readChunk(client, 300);  // expect a "Password:" prompt
    client.print(pass);
    client.print("\r\n");

    String response = readChunk(client, 500);
    client.stop();

    // Heuristic, not a protocol-level guarantee — telnetd login prompts
    // vary a lot across implementations (see README). A failure message
    // is strong negative evidence; its absence alongside something that
    // looks like a shell prompt is (weak) positive evidence. Requiring
    // both conditions biases this toward false negatives over false
    // positives on purpose: wrongly clearing a vulnerable device is bad,
    // but wrongly flagging one that's actually fine erodes trust in
    // every other finding this tool reports.
    String lower = response;
    lower.toLowerCase();
    bool looksFailed = lower.indexOf("incorrect") >= 0 || lower.indexOf("fail") >= 0 ||
                        lower.indexOf("denied") >= 0 || lower.indexOf("invalid") >= 0;
    bool looksLikePrompt = response.indexOf('$') >= 0 || response.indexOf('#') >= 0 ||
                            response.indexOf('>') >= 0;

    return !looksFailed && looksLikePrompt;
}

bool CredAuditManager::tryFtpLogin(const IPAddress& ip, const String& user, const String& pass) {
    WiFiClient client;
    if (!client.connect(ip, 21, g_config.scanTimeoutMs)) return false;

    readLine(client, g_config.scanTimeoutMs);  // "220 ..." welcome banner

    client.print("USER ");
    client.print(user);
    client.print("\r\n");
    String resp = readLine(client, g_config.scanTimeoutMs);

    if (resp.startsWith("230")) {
        // Some servers accept the username alone (e.g. anonymous-style).
        client.stop();
        return true;
    }
    if (!resp.startsWith("331")) {
        client.stop();  // user rejected outright — don't bother sending a password
        return false;
    }

    client.print("PASS ");
    client.print(pass);
    client.print("\r\n");
    resp = readLine(client, g_config.scanTimeoutMs);
    client.stop();

    return resp.startsWith("230");  // RFC 959: 230 = user logged in, proceed
}

void CredAuditManager::logAttempt(const char* service, const String& user, const String& pass, bool success) {
    String combo = user + ":" + pass;
    if (combo.length() > 28) combo = combo.substring(0, 28);  // leaves room for " FAIL"/" OK" in the 40-byte field

    ScanNotification n;
    n.source = ScanSource::CredAudit;
    n.type = ScanEventType::LogLine;
    n.setText((combo + (success ? " OK" : " FAIL")).c_str());
    (void)service;  // not enough room in the 40-char field alongside user:pass; the screen already shows the target/service header

    if (_outQueue) xQueueSend(_outQueue, &n, 0);
}

void CredAuditManager::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::CredAudit;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}
