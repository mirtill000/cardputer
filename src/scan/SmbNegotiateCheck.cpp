#include "SmbNegotiateCheck.h"
#include <WiFiClient.h>
#include <cstring>

SmbNegotiateCheck g_smbCheck;

namespace {

constexpr uint16_t kConnectTimeoutMs = 3000;
constexpr uint16_t kReadTimeoutMs = 3000;

// Appends a "\x02<dialect>\0" entry to buf and returns the new length.
size_t appendDialect(uint8_t* buf, size_t n, const char* dialect) {
    buf[n++] = 0x02;  // SMB "Dialect" buffer format byte
    size_t len = strlen(dialect);
    memcpy(buf + n, dialect, len);
    n += len;
    buf[n++] = 0x00;  // NUL terminator
    return n;
}

// Builds a complete SMB1 SMB_COM_NEGOTIATE request (NetBIOS session
// header + SMB header + dialect list) into buf. Returns total length.
size_t buildNegotiate(uint8_t* buf) {
    // --- SMB message (header + body) is assembled first, from offset 4,
    // so the 4-byte NetBIOS length prefix can be filled in at the end. ---
    size_t n = 4;

    // SMB header (32 bytes).
    buf[n++] = 0xFF; buf[n++] = 'S'; buf[n++] = 'M'; buf[n++] = 'B';  // protocol id
    buf[n++] = 0x72;  // SMB_COM_NEGOTIATE
    buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x00;  // NT_STATUS = success
    buf[n++] = 0x18;  // Flags
    buf[n++] = 0x01; buf[n++] = 0x28;  // Flags2 (0x2801: long names, NT status, unicode)
    buf[n++] = 0x00; buf[n++] = 0x00;  // PIDHigh
    for (int i = 0; i < 8; i++) buf[n++] = 0x00;  // SecuritySignature
    buf[n++] = 0x00; buf[n++] = 0x00;  // Reserved
    buf[n++] = 0x00; buf[n++] = 0x00;  // TID
    buf[n++] = 0xFF; buf[n++] = 0xFE;  // PIDLow (arbitrary)
    buf[n++] = 0x00; buf[n++] = 0x00;  // UID
    buf[n++] = 0x00; buf[n++] = 0x00;  // MID

    // Body: WordCount(0), ByteCount, then the dialect strings.
    buf[n++] = 0x00;  // WordCount = 0
    size_t byteCountPos = n;
    n += 2;  // ByteCount placeholder, filled in below
    size_t dialectStart = n;
    n = appendDialect(buf, n, "PC NETWORK PROGRAM 1.0");
    n = appendDialect(buf, n, "LANMAN1.0");
    n = appendDialect(buf, n, "NT LM 0.12");
    uint16_t byteCount = (uint16_t)(n - dialectStart);
    buf[byteCountPos] = (uint8_t)(byteCount & 0xFF);
    buf[byteCountPos + 1] = (uint8_t)(byteCount >> 8);

    // NetBIOS session header: type 0x00 (session message) + 24-bit length
    // of the SMB message that follows. Used on both 139 and direct-hosted
    // 445 transports.
    uint32_t smbLen = (uint32_t)(n - 4);
    buf[0] = 0x00;
    buf[1] = (uint8_t)((smbLen >> 16) & 0xFF);
    buf[2] = (uint8_t)((smbLen >> 8) & 0xFF);
    buf[3] = (uint8_t)(smbLen & 0xFF);

    return n;
}

}  // namespace

void SmbNegotiateCheck::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

bool SmbNegotiateCheck::start(const IPAddress& ip, uint16_t port) {
    if (_running) {
        notify("SMB check already running");
        return false;
    }
    _ip = ip;
    _port = port;
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _result = Result{};  // reset
        xSemaphoreGive(_mutex);
    }
    _running = true;
    notify(ScanEventType::ScanStarted);
    if (xTaskCreatePinnedToCore(&SmbNegotiateCheck::taskEntry, "smbneg", 4096, this, 1, nullptr, 0) != pdPASS) {
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return false;
    }
    return true;
}

void SmbNegotiateCheck::taskEntry(void* arg) {
    static_cast<SmbNegotiateCheck*>(arg)->run();
    vTaskDelete(nullptr);
}

void SmbNegotiateCheck::run() {
    Result r;

    WiFiClient client;
    notify(String("connecting to ") + _ip.toString() + ":" + String(_port));
    if (!client.connect(_ip, _port, kConnectTimeoutMs)) {
        r.done = true;
        r.note = "connect failed (port closed/filtered)";
        notify(r.note);
    } else {
        r.connected = true;

        uint8_t req[128];
        size_t reqLen = buildNegotiate(req);
        client.write(req, reqLen);

        // Read the response. 64 bytes is plenty to reach SecurityMode
        // (which sits at offset 39 in the NT LM 0.12 response).
        uint8_t resp[256];
        size_t got = 0;
        uint32_t start = millis();
        while ((millis() - start) < kReadTimeoutMs && got < sizeof(resp)) {
            if (client.available()) {
                int c = client.read();
                if (c < 0) break;
                resp[got++] = (uint8_t)c;
            } else if (got >= 40) {
                break;  // already have enough to parse SecurityMode
            } else {
                delay(10);
            }
        }
        client.stop();

        // Validate: NetBIOS(4) + SMB header must be present and be a
        // negotiate response with SMB signature "\xffSMB" and command 0x72.
        if (got >= 40 && resp[4] == 0xFF && resp[5] == 'S' && resp[6] == 'M' && resp[7] == 'B' &&
            resp[8] == 0x72) {
            uint8_t wordCount = resp[36];
            if (wordCount >= 2) {
                r.negotiated = true;
                r.dialectIndex = (int16_t)(resp[37] | (resp[38] << 8));
                uint8_t securityMode = resp[39];
                r.userLevelSecurity = (securityMode & 0x01) != 0;
                r.challengeResponse = (securityMode & 0x02) != 0;
                r.signingEnabled = (securityMode & 0x04) != 0;
                r.signingRequired = (securityMode & 0x08) != 0;

                String s = r.userLevelSecurity ? "user-level" : "SHARE-LEVEL(!)";
                s += r.challengeResponse ? ", chal/resp" : ", PLAINTEXT(!)";
                s += r.signingRequired ? ", signed" : (r.signingEnabled ? ", sign-opt" : ", unsigned");
                r.note = s;
            } else {
                r.note = "negotiate ok but no security data";
            }
        } else if (got > 0) {
            r.note = "response not SMB1 (SMB2+/non-SMB?)";
        } else {
            r.note = "no response to negotiate";
        }
        r.done = true;
        notify(r.note);
    }

    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _result = r;
        xSemaphoreGive(_mutex);
    }
    _running = false;
    notify(ScanEventType::ScanFinished, 100);
}

void SmbNegotiateCheck::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Smb;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

void SmbNegotiateCheck::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Smb;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}

SmbNegotiateCheck::Result SmbNegotiateCheck::result() const {
    Result r;
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        r = _result;
        xSemaphoreGive(_mutex);
    }
    return r;
}
