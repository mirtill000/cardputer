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
size_t buildSmb1Negotiate(uint8_t* buf) {
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

// Builds an SMB2 NEGOTIATE request offering 2.0.2 / 2.1 / 3.0 / 3.0.2
// (deliberately NOT 3.1.1, so no preauth-integrity negotiate contexts
// are required). Returns total length including the 4-byte NetBIOS
// session header. Layout per MS-SMB2 2.2.1.2 (header) + 2.2.3 (body).
size_t buildSmb2Negotiate(uint8_t* buf) {
    size_t n = 4;  // NetBIOS header filled in at the end

    // SMB2 SYNC header (64 bytes).
    buf[n++] = 0xFE; buf[n++] = 'S'; buf[n++] = 'M'; buf[n++] = 'B';  // ProtocolId
    buf[n++] = 0x40; buf[n++] = 0x00;  // StructureSize = 64
    buf[n++] = 0x00; buf[n++] = 0x00;  // CreditCharge
    buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x00;  // Status / ChannelSequence
    buf[n++] = 0x00; buf[n++] = 0x00;  // Command = NEGOTIATE (0)
    buf[n++] = 0x00; buf[n++] = 0x00;  // CreditRequest
    buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x00;  // Flags
    buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x00;  // NextCommand
    for (int i = 0; i < 8; i++) buf[n++] = 0x00;  // MessageId
    buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x00;  // Reserved (ProcessId)
    buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x00;  // TreeId
    for (int i = 0; i < 8; i++) buf[n++] = 0x00;   // SessionId
    for (int i = 0; i < 16; i++) buf[n++] = 0x00;  // Signature

    // NEGOTIATE request body: 36 fixed bytes + the dialect array.
    buf[n++] = 0x24; buf[n++] = 0x00;  // StructureSize = 36
    buf[n++] = 0x04; buf[n++] = 0x00;  // DialectCount = 4
    buf[n++] = 0x01; buf[n++] = 0x00;  // SecurityMode = SIGNING_ENABLED
    buf[n++] = 0x00; buf[n++] = 0x00;  // Reserved
    buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x00;  // Capabilities
    for (int i = 0; i < 16; i++) buf[n++] = 0x00;  // ClientGuid
    for (int i = 0; i < 8; i++) buf[n++] = 0x00;   // ClientStartTime (no negotiate contexts)
    buf[n++] = 0x02; buf[n++] = 0x02;  // dialect 0x0202
    buf[n++] = 0x10; buf[n++] = 0x02;  // dialect 0x0210
    buf[n++] = 0x00; buf[n++] = 0x03;  // dialect 0x0300
    buf[n++] = 0x02; buf[n++] = 0x03;  // dialect 0x0302

    uint32_t smbLen = (uint32_t)(n - 4);
    buf[0] = 0x00;
    buf[1] = (uint8_t)((smbLen >> 16) & 0xFF);
    buf[2] = (uint8_t)((smbLen >> 8) & 0xFF);
    buf[3] = (uint8_t)(smbLen & 0xFF);
    return n;
}

// Reads up to cap bytes for up to kReadTimeoutMs, stopping early once at
// least minBytes have arrived and the peer pauses - same heuristic the
// original single-phase reader used.
size_t readResponse(WiFiClient& c, uint8_t* buf, size_t cap, size_t minBytes) {
    size_t got = 0;
    uint32_t start = millis();
    while ((millis() - start) < kReadTimeoutMs && got < cap) {
        if (c.available()) {
            int ch = c.read();
            if (ch < 0) break;
            buf[got++] = (uint8_t)ch;
        } else if (got >= minBytes) {
            break;
        } else {
            delay(10);
        }
    }
    return got;
}

// Parses an SMB2 NEGOTIATE response: verifies the SMB2 signature, a
// NEGOTIATE command and success status, then pulls DialectRevision and
// SecurityMode out of the response body. Fails closed on anything off.
bool parseSmb2Negotiate(const uint8_t* resp, size_t len, int16_t& dialectOut, uint16_t& securityModeOut) {
    if (len < 4 + 64 + 6) return false;  // need through DialectRevision
    if (!(resp[4] == 0xFE && resp[5] == 'S' && resp[6] == 'M' && resp[7] == 'B')) return false;
    uint16_t command = (uint16_t)(resp[4 + 12] | (resp[4 + 13] << 8));
    if (command != 0) return false;  // NEGOTIATE
    uint32_t status = (uint32_t)resp[4 + 8] | ((uint32_t)resp[4 + 9] << 8) |
                      ((uint32_t)resp[4 + 10] << 16) | ((uint32_t)resp[4 + 11] << 24);
    if (status != 0) return false;  // STATUS_SUCCESS
    size_t body = 4 + 64;
    uint16_t structSize = (uint16_t)(resp[body] | (resp[body + 1] << 8));
    if (structSize != 65) return false;  // MS-SMB2: negotiate response StructureSize is 65
    securityModeOut = (uint16_t)(resp[body + 2] | (resp[body + 3] << 8));
    dialectOut = (int16_t)(resp[body + 4] | (resp[body + 5] << 8));
    return true;
}

// Derives the overall verdict and a short reason line from the two
// negotiate exchanges.
void computePosture(SmbNegotiateCheck::Result& r) {
    if (!r.connected) {
        r.posture = SmbNegotiateCheck::Posture::Unknown;
        r.postureNote = "SMB: not reachable";
        r.note = r.postureNote;
        return;
    }
    if (!r.smb1Enabled && !r.smb2Supported) {
        r.posture = SmbNegotiateCheck::Posture::Unknown;
        r.postureNote = "SMB: no negotiate reply";
        r.note = r.postureNote;
        return;
    }

    bool weak = r.smb1Enabled ||
                (r.negotiated && !r.userLevelSecurity) ||  // share-level
                (r.negotiated && !r.challengeResponse);     // plaintext
    bool signingWeak = r.smb2Supported ? !r.smb2SigningRequired
                                       : (r.negotiated ? !r.signingRequired : true);

    if (weak) r.posture = SmbNegotiateCheck::Posture::Weak;
    else if (signingWeak) r.posture = SmbNegotiateCheck::Posture::Fair;
    else r.posture = SmbNegotiateCheck::Posture::Ok;

    String reason;
    auto add = [&](const char* s) {
        if (reason.length()) reason += ", ";
        reason += s;
    };
    if (r.smb1Enabled) add("SMBv1 on");
    if (r.negotiated && !r.userLevelSecurity) add("share-level");
    if (r.negotiated && !r.challengeResponse) add("plaintext");
    if (signingWeak) add("signing not required");
    if (!reason.length()) reason = "SMBv1 off, signing required";

    const char* label = (r.posture == SmbNegotiateCheck::Posture::Weak) ? "WEAK"
                      : (r.posture == SmbNegotiateCheck::Posture::Fair) ? "FAIR" : "OK";
    r.postureNote = String("posture: ") + label + " (" + reason + ")";
    r.note = r.postureNote;
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

    // ---- Phase 1: SMB1 negotiate (also the SMBv1-enabled probe) ----
    {
        WiFiClient client;
        notify(String("SMB1 negotiate ") + _ip.toString() + ":" + String(_port));
        if (client.connect(_ip, _port, kConnectTimeoutMs)) {
            r.connected = true;

            uint8_t req[128];
            size_t reqLen = buildSmb1Negotiate(req);
            client.write(req, reqLen);

            // 64 bytes is plenty to reach SecurityMode (offset 39 in the
            // NT LM 0.12 response).
            uint8_t resp[256];
            size_t got = readResponse(client, resp, sizeof(resp), 40);
            client.stop();

            // Validate: NetBIOS(4) + SMB header, a negotiate response with
            // SMB signature "\xffSMB" and command 0x72.
            if (got >= 40 && resp[4] == 0xFF && resp[5] == 'S' && resp[6] == 'M' && resp[7] == 'B' &&
                resp[8] == 0x72) {
                uint8_t wordCount = resp[36];
                if (wordCount >= 2) {
                    r.negotiated = true;
                    r.smb1Enabled = true;  // the server still answers SMBv1 at all
                    r.dialectIndex = (int16_t)(resp[37] | (resp[38] << 8));
                    uint8_t securityMode = resp[39];
                    r.userLevelSecurity = (securityMode & 0x01) != 0;
                    r.challengeResponse = (securityMode & 0x02) != 0;
                    r.signingEnabled = (securityMode & 0x04) != 0;
                    r.signingRequired = (securityMode & 0x08) != 0;
                    notify("SMBv1 ENABLED (legacy exposure)");
                } else {
                    notify("SMB1 negotiate ok, no security data");
                }
            } else if (got > 0) {
                notify("no SMBv1 (SMB2+/non-SMB reply)");
            } else {
                notify("no reply to SMB1 negotiate");
            }
        } else {
            notify("connect failed (port closed/filtered)");
        }
    }

    // ---- Phase 2: SMB2 negotiate (only if the port was reachable) ----
    if (r.connected) {
        WiFiClient client;
        notify("SMB2 negotiate...");
        if (client.connect(_ip, _port, kConnectTimeoutMs)) {
            uint8_t req[128];
            size_t reqLen = buildSmb2Negotiate(req);
            client.write(req, reqLen);

            uint8_t resp[256];
            size_t got = readResponse(client, resp, sizeof(resp), 74);
            client.stop();

            int16_t rev = -1;
            uint16_t sm2 = 0;
            if (parseSmb2Negotiate(resp, got, rev, sm2)) {
                r.smb2Supported = true;
                r.smb2Dialect = rev;
                r.smb2SigningEnabled = (sm2 & 0x0001) != 0;
                r.smb2SigningRequired = (sm2 & 0x0002) != 0;
                notify(String("SMB2 ") + dialectName(rev) +
                       (r.smb2SigningRequired ? " sign:req" : (r.smb2SigningEnabled ? " sign:opt" : " sign:off")));
            } else {
                notify("no SMB2 negotiate response");
            }
        }
    }

    // ---- Verdict ----
    computePosture(r);
    r.done = true;
    notify(r.postureNote);

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

String SmbNegotiateCheck::dialectName(int16_t smb2Dialect) {
    switch ((uint16_t)smb2Dialect) {
        case 0x0202: return "2.0.2";
        case 0x0210: return "2.1";
        case 0x0300: return "3.0";
        case 0x0302: return "3.0.2";
        case 0x0311: return "3.1.1";
        case 0x02FF: return "2.x*";
        default: return String("0x") + String((unsigned)(uint16_t)smb2Dialect, HEX);
    }
}
