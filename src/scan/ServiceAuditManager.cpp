#include "ServiceAuditManager.h"
#include "ScanManager.h"
#include "Base64.h"
#include "../core/Types.h"
#include <WiFiClient.h>
#include <mbedtls/md.h>
#include <cstring>

ServiceAuditManager g_serviceAuditManager;

namespace {

constexpr uint16_t kConnectTimeoutMs = 2500;
constexpr uint16_t kReadTimeoutMs = 2500;
constexpr uint16_t kAttemptDelayMs = 150;  // polite rate-limit between tries

// ---- small helpers ----------------------------------------------------

void mdHash(mbedtls_md_type_t type, const uint8_t* data, size_t len, uint8_t* out) {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(type);
    if (info) mbedtls_md(info, data, len, out);
}

String toHex(const uint8_t* data, size_t len) {
    static const char* hx = "0123456789abcdef";
    String s;
    for (size_t i = 0; i < len; i++) {
        s += hx[data[i] >> 4];
        s += hx[data[i] & 0x0F];
    }
    return s;
}

// Reads exactly n bytes (or times out). Returns bytes actually read.
int readN(WiFiClient& c, uint8_t* buf, int n, uint16_t timeoutMs) {
    int got = 0;
    uint32_t start = millis();
    while (got < n && (millis() - start) < timeoutMs) {
        if (c.available()) {
            int r = c.read(buf + got, n - got);
            if (r > 0) {
                got += r;
                continue;
            }
        }
        delay(5);
    }
    return got;
}

// Reads one CRLF-terminated line (without the CRLF). "" on timeout.
String readLine(WiFiClient& c, uint16_t timeoutMs) {
    String out;
    uint32_t start = millis();
    while ((millis() - start) < timeoutMs && out.length() < 256) {
        if (c.available()) {
            char ch = (char)c.read();
            if (ch == '\n') break;
            if (ch != '\r') out += ch;
        } else {
            delay(5);
        }
    }
    return out;
}

// ---- FTP --------------------------------------------------------------

// Returns: 0 = anonymous denied, 1 = anonymous allowed, 2 = allowed+writable.
int ftpAnonymous(const IPAddress& ip, uint16_t port) {
    WiFiClient c;
    if (!c.connect(ip, port, kConnectTimeoutMs)) return -1;
    readLine(c, kReadTimeoutMs);  // 220 banner
    c.print("USER anonymous\r\n");
    readLine(c, kReadTimeoutMs);  // 331
    c.print("PASS anonymous@example.com\r\n");
    String r = readLine(c, kReadTimeoutMs);
    int result = 0;
    if (r.startsWith("230")) {
        result = 1;
        // Writable test: try to create then remove a throwaway directory.
        c.print("MKD nettest_probe\r\n");
        String m = readLine(c, kReadTimeoutMs);
        if (m.startsWith("257")) {
            result = 2;
            c.print("RMD nettest_probe\r\n");
            readLine(c, kReadTimeoutMs);
        }
    }
    c.print("QUIT\r\n");
    c.stop();
    return result;
}

// ---- Redis ------------------------------------------------------------

// Returns 1 no-auth, 2 default-password hit (pass in outPass), 0 locked, -1 unreachable.
int redisCheck(const IPAddress& ip, uint16_t port, String& outPass) {
    WiFiClient c;
    if (!c.connect(ip, port, kConnectTimeoutMs)) return -1;
    c.print("PING\r\n");
    String r = readLine(c, kReadTimeoutMs);
    if (r.startsWith("+PONG")) {
        c.stop();
        return 1;
    }
    if (r.indexOf("NOAUTH") >= 0 || r.indexOf("Authentication") >= 0) {
        const char* pws[] = {"redis", "password", "admin", "root", "foobared", "changeme"};
        for (const char* pw : pws) {
            c.print(String("AUTH ") + pw + "\r\n");
            String a = readLine(c, kReadTimeoutMs);
            if (a.startsWith("+OK")) {
                outPass = pw;
                c.stop();
                return 2;
            }
            delay(kAttemptDelayMs);
        }
        c.stop();
        return 0;
    }
    c.stop();
    return -1;
}

// ---- MySQL (mysql_native_password) -----------------------------------

// One full connect + greeting parse + auth attempt. Returns 1 valid,
// 0 invalid, -1 unreachable/unsupported (note filled).
int mysqlTry(const IPAddress& ip, uint16_t port, const String& user, const String& pass, String& note) {
    WiFiClient c;
    if (!c.connect(ip, port, kConnectTimeoutMs)) return -1;

    uint8_t hdr[4];
    if (readN(c, hdr, 4, kReadTimeoutMs) != 4) {
        c.stop();
        return -1;
    }
    int plen = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16);
    if (plen <= 0 || plen > 512) {
        c.stop();
        return -1;
    }
    uint8_t p[512];
    if (readN(c, p, plen, kReadTimeoutMs) != plen) {
        c.stop();
        return -1;
    }

    // Parse handshake v10.
    int idx = 0;
    if (p[idx++] != 10) {
        c.stop();
        return -1;
    }
    while (idx < plen && p[idx] != 0) idx++;  // server version string
    idx++;                                     // null
    idx += 4;                                  // thread id
    if (idx + 8 > plen) {
        c.stop();
        return -1;
    }
    uint8_t scramble[20];
    memcpy(scramble, p + idx, 8);
    idx += 8;
    idx++;                                      // filler
    idx += 2;                                   // cap low
    idx += 1;                                   // charset
    idx += 2;                                   // status
    idx += 2;                                   // cap high
    int authDataLen = (idx < plen) ? p[idx] : 0;
    idx += 1;
    idx += 10;                                  // reserved
    int part2 = authDataLen - 8;
    if (part2 < 13) part2 = 13;
    if (idx + 12 > plen) {
        c.stop();
        return -1;
    }
    memcpy(scramble + 8, p + idx, 12);
    idx += part2;
    // Auth plugin name (if present).
    String plugin;
    while (idx < plen && p[idx] != 0) plugin += (char)p[idx++];
    if (plugin.length() && plugin != "mysql_native_password") {
        note = String("auth plugin ") + plugin + " unsupported";
        c.stop();
        return -1;
    }

    // token = SHA1(pass) XOR SHA1(scramble + SHA1(SHA1(pass)))
    uint8_t token[20] = {0};
    if (pass.length()) {
        uint8_t h1[20], h2[20], h3[20], tmp[40];
        mdHash(MBEDTLS_MD_SHA1, (const uint8_t*)pass.c_str(), pass.length(), h1);
        mdHash(MBEDTLS_MD_SHA1, h1, 20, h2);
        memcpy(tmp, scramble, 20);
        memcpy(tmp + 20, h2, 20);
        mdHash(MBEDTLS_MD_SHA1, tmp, 40, h3);
        for (int i = 0; i < 20; i++) token[i] = h1[i] ^ h3[i];
    }

    // Handshake response.
    std::vector<uint8_t> body;
    uint32_t cap = 0x0008A205;  // PROTOCOL_41|SECURE_CONNECTION|PLUGIN_AUTH|LONG_PASSWORD|LONG_FLAG|TRANSACTIONS
    body.push_back(cap & 0xFF);
    body.push_back((cap >> 8) & 0xFF);
    body.push_back((cap >> 16) & 0xFF);
    body.push_back((cap >> 24) & 0xFF);
    for (int i = 0; i < 4; i++) body.push_back(i == 2 ? 0x01 : 0x00);  // max packet 16MB
    body.push_back(0x21);                                              // charset utf8
    for (int i = 0; i < 23; i++) body.push_back(0x00);                 // reserved
    for (size_t i = 0; i < user.length(); i++) body.push_back(user[i]);
    body.push_back(0x00);
    if (pass.length()) {
        body.push_back(20);
        for (int i = 0; i < 20; i++) body.push_back(token[i]);
    } else {
        body.push_back(0x00);  // empty auth response
    }
    const char* pn = "mysql_native_password";
    for (size_t i = 0; i < strlen(pn); i++) body.push_back(pn[i]);
    body.push_back(0x00);

    uint8_t oh[4];
    oh[0] = body.size() & 0xFF;
    oh[1] = (body.size() >> 8) & 0xFF;
    oh[2] = (body.size() >> 16) & 0xFF;
    oh[3] = 1;  // sequence 1
    c.write(oh, 4);
    c.write(body.data(), body.size());

    // Read reply packet.
    if (readN(c, hdr, 4, kReadTimeoutMs) != 4) {
        c.stop();
        return -1;
    }
    int rlen = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16);
    if (rlen <= 0 || rlen > 512) {
        c.stop();
        return -1;
    }
    uint8_t rp[512];
    int got = readN(c, rp, rlen, kReadTimeoutMs);
    c.stop();
    if (got < 1) return -1;
    if (rp[0] == 0x00) return 1;  // OK packet = auth success
    return 0;                     // 0xFF ERR (or auth-switch 0xFE) = fail
}

// ---- PostgreSQL -------------------------------------------------------

int32_t be32(const uint8_t* b) {
    return ((int32_t)b[0] << 24) | ((int32_t)b[1] << 16) | ((int32_t)b[2] << 8) | b[3];
}

// Returns 1 valid, 0 invalid, -1 unreachable, -2 SCRAM (unsupported).
int postgresTry(const IPAddress& ip, uint16_t port, const String& user, const String& pass) {
    WiFiClient c;
    if (!c.connect(ip, port, kConnectTimeoutMs)) return -1;

    // StartupMessage: protocol 3.0, user + database (= user).
    std::vector<uint8_t> params;
    auto addStr = [&](const char* s) {
        for (size_t i = 0; s[i]; i++) params.push_back((uint8_t)s[i]);
        params.push_back(0);
    };
    addStr("user");
    addStr(user.c_str());
    addStr("database");
    addStr(user.c_str());
    params.push_back(0);  // end of params

    uint32_t total = 4 + 4 + params.size();
    std::vector<uint8_t> msg;
    msg.push_back((total >> 24) & 0xFF);
    msg.push_back((total >> 16) & 0xFF);
    msg.push_back((total >> 8) & 0xFF);
    msg.push_back(total & 0xFF);
    msg.push_back(0x00);
    msg.push_back(0x03);
    msg.push_back(0x00);
    msg.push_back(0x00);  // protocol 196608
    msg.insert(msg.end(), params.begin(), params.end());
    c.write(msg.data(), msg.size());

    // Read one message: type(1) + len(4 BE) + payload(len-4).
    uint8_t type;
    if (readN(c, &type, 1, kReadTimeoutMs) != 1) {
        c.stop();
        return -1;
    }
    uint8_t lb[4];
    if (readN(c, lb, 4, kReadTimeoutMs) != 4) {
        c.stop();
        return -1;
    }
    int len = be32(lb) - 4;
    if (len < 0 || len > 512) {
        c.stop();
        return -1;
    }
    uint8_t pl[512];
    if (len > 0 && readN(c, pl, len, kReadTimeoutMs) != len) {
        c.stop();
        return -1;
    }

    if (type != 'R') {  // e.g. 'E' error
        c.stop();
        return 0;
    }
    int32_t authType = be32(pl);
    if (authType == 0) {  // AuthenticationOk with no password (trust)
        c.stop();
        return 1;
    }

    std::vector<uint8_t> pwmsg;
    if (authType == 3) {  // cleartext
        String body = pass;
        uint32_t l = 4 + body.length() + 1;
        pwmsg.push_back('p');
        pwmsg.push_back((l >> 24) & 0xFF);
        pwmsg.push_back((l >> 16) & 0xFF);
        pwmsg.push_back((l >> 8) & 0xFF);
        pwmsg.push_back(l & 0xFF);
        for (size_t i = 0; i < body.length(); i++) pwmsg.push_back(body[i]);
        pwmsg.push_back(0);
    } else if (authType == 5) {  // MD5, salt in pl[4..7]
        uint8_t inner[16], outer[16];
        String pu = pass + user;
        mdHash(MBEDTLS_MD_MD5, (const uint8_t*)pu.c_str(), pu.length(), inner);
        String innerHex = toHex(inner, 16);
        std::vector<uint8_t> salted;
        for (size_t i = 0; i < innerHex.length(); i++) salted.push_back(innerHex[i]);
        for (int i = 0; i < 4; i++) salted.push_back(pl[4 + i]);
        mdHash(MBEDTLS_MD_MD5, salted.data(), salted.size(), outer);
        String body = String("md5") + toHex(outer, 16);
        uint32_t l = 4 + body.length() + 1;
        pwmsg.push_back('p');
        pwmsg.push_back((l >> 24) & 0xFF);
        pwmsg.push_back((l >> 16) & 0xFF);
        pwmsg.push_back((l >> 8) & 0xFF);
        pwmsg.push_back(l & 0xFF);
        for (size_t i = 0; i < body.length(); i++) pwmsg.push_back(body[i]);
        pwmsg.push_back(0);
    } else if (authType == 10) {  // SASL / SCRAM
        c.stop();
        return -2;
    } else {
        c.stop();
        return 0;
    }

    c.write(pwmsg.data(), pwmsg.size());
    // Expect 'R' AuthenticationOk (authType 0) on success, else 'E'.
    if (readN(c, &type, 1, kReadTimeoutMs) != 1) {
        c.stop();
        return -1;
    }
    if (readN(c, lb, 4, kReadTimeoutMs) != 4) {
        c.stop();
        return -1;
    }
    len = be32(lb) - 4;
    if (len < 0 || len > 512) {
        c.stop();
        return -1;
    }
    if (len > 0) readN(c, pl, len, kReadTimeoutMs);
    c.stop();
    if (type == 'R' && be32(pl) == 0) return 1;
    return 0;
}

// ---- VNC (RFB) --------------------------------------------------------

// Returns 1 = no authentication (security type "None" offered), 0 = auth
// required, -1 = unreachable.
//
// The DES challenge/response brute against default passwords was dropped:
// ESP-IDF's precompiled mbedtls ships with MBEDTLS_DES_C disabled (DES is
// deprecated), so mbedtls_des_* won't link, and hand-rolling DES would go
// against this project's "no artisanal crypto" rule. The high-value part —
// a VNC server offering NO authentication at all (full remote control with
// no password) — needs no crypto and is what this reports.
int vncCheck(const IPAddress& ip, uint16_t port) {
    WiFiClient c;
    if (!c.connect(ip, port, kConnectTimeoutMs)) return -1;
    uint8_t ver[12];
    if (readN(c, ver, 12, kReadTimeoutMs) != 12) {
        c.stop();
        return -1;
    }
    c.write((const uint8_t*)"RFB 003.008\n", 12);
    uint8_t nTypes = 0;
    if (readN(c, &nTypes, 1, kReadTimeoutMs) != 1 || nTypes == 0) {
        c.stop();
        return -1;
    }
    uint8_t types[32];
    int rd = readN(c, types, nTypes > 32 ? 32 : nTypes, kReadTimeoutMs);
    c.stop();
    for (int i = 0; i < rd; i++)
        if (types[i] == 1) return 1;  // "None" security type = no auth
    return 0;
}

// ---- HTTP Basic-auth --------------------------------------------------

int httpStatus(WiFiClient& c, uint16_t timeoutMs) {
    String line = readLine(c, timeoutMs);
    int sp = line.indexOf(' ');
    if (sp <= 0) return 0;
    return line.substring(sp + 1, sp + 4).toInt();
}

// Returns 1 hit (outCred "user:pass"), 0 no default worked, -1 no basic-auth/unreachable.
int httpBasic(const IPAddress& ip, uint16_t port, String& outCred) {
    {
        WiFiClient c;
        if (!c.connect(ip, port, kConnectTimeoutMs)) return -1;
        c.print("GET / HTTP/1.0\r\nHost: h\r\nConnection: close\r\n\r\n");
        int code = httpStatus(c, kReadTimeoutMs);
        bool basic = false;
        // Scan headers for WWW-Authenticate: Basic
        for (int i = 0; i < 30; i++) {
            String h = readLine(c, kReadTimeoutMs);
            if (h.length() == 0) break;
            String lower = h;
            lower.toLowerCase();
            if (lower.startsWith("www-authenticate:") && lower.indexOf("basic") >= 0) basic = true;
        }
        c.stop();
        if (code != 401 || !basic) return -1;
    }

    struct Cred {
        const char* u;
        const char* p;
    };
    const Cred creds[] = {{"admin", "admin"}, {"admin", ""},     {"admin", "password"}, {"root", "root"},
                          {"user", "user"},   {"admin", "1234"}, {"admin", "12345"}};
    for (const Cred& cr : creds) {
        WiFiClient c;
        if (!c.connect(ip, port, kConnectTimeoutMs)) return -1;
        String token = b64::encode(String(cr.u) + ":" + cr.p);
        c.print(String("GET / HTTP/1.0\r\nHost: h\r\nAuthorization: Basic ") + token +
                "\r\nConnection: close\r\n\r\n");
        int code = httpStatus(c, kReadTimeoutMs);
        c.stop();
        if (code && code != 401 && code != 403) {
            outCred = String(cr.u) + ":" + cr.p;
            return 1;
        }
        delay(kAttemptDelayMs);
    }
    return 0;
}

// ---- SMB1 null session ------------------------------------------------

// Builds the SMB1 NEGOTIATE (same as SmbNegotiateCheck) into buf, returns len.
size_t smbBuildNegotiate(uint8_t* buf) {
    size_t n = 4;  // leave room for NetBIOS header
    buf[n++] = 0xFF;
    buf[n++] = 'S';
    buf[n++] = 'M';
    buf[n++] = 'B';
    buf[n++] = 0x72;  // NEGOTIATE
    for (int i = 0; i < 4; i++) buf[n++] = 0x00;  // status
    buf[n++] = 0x18;
    buf[n++] = 0x01;
    buf[n++] = 0x28;  // flags2
    for (int i = 0; i < 2; i++) buf[n++] = 0x00;   // PIDHigh
    for (int i = 0; i < 8; i++) buf[n++] = 0x00;   // signature
    for (int i = 0; i < 2; i++) buf[n++] = 0x00;   // reserved
    for (int i = 0; i < 2; i++) buf[n++] = 0x00;   // TID
    buf[n++] = 0xFF;
    buf[n++] = 0xFE;                               // PIDLow
    for (int i = 0; i < 2; i++) buf[n++] = 0x00;   // UID
    for (int i = 0; i < 2; i++) buf[n++] = 0x00;   // MID
    buf[n++] = 0x00;                               // WordCount
    size_t bcPos = n;
    n += 2;
    const char* dialects[] = {"\x02PC NETWORK PROGRAM 1.0", "\x02LANMAN1.0", "\x02NT LM 0.12"};
    size_t start = n;
    for (const char* d : dialects) {
        buf[n++] = 0x02;
        const char* s = d + 1;
        for (size_t i = 0; s[i]; i++) buf[n++] = s[i];
        buf[n++] = 0x00;
    }
    uint16_t bc = n - start;
    buf[bcPos] = bc & 0xFF;
    buf[bcPos + 1] = bc >> 8;
    uint32_t smbLen = n - 4;
    buf[0] = 0x00;
    buf[1] = (smbLen >> 16) & 0xFF;
    buf[2] = (smbLen >> 8) & 0xFF;
    buf[3] = smbLen & 0xFF;
    return n;
}

// Returns 1 anonymous session accepted, 0 denied, -1 unreachable/skipped (note set).
int smbNullSession(const IPAddress& ip, uint16_t port, String& note) {
    WiFiClient c;
    if (!c.connect(ip, port, kConnectTimeoutMs)) return -1;

    uint8_t req[128];
    size_t reqLen = smbBuildNegotiate(req);
    c.write(req, reqLen);

    uint8_t resp[256];
    int got = readN(c, resp, 4, kReadTimeoutMs);  // NetBIOS header
    if (got != 4) {
        c.stop();
        return -1;
    }
    int mlen = (resp[1] << 16) | (resp[2] << 8) | resp[3];
    if (mlen < 33 || mlen > 250) {
        c.stop();
        return -1;
    }
    if (readN(c, resp, mlen, kReadTimeoutMs) != mlen) {
        c.stop();
        return -1;
    }
    if (!(resp[0] == 0xFF && resp[1] == 'S' && resp[2] == 'M' && resp[3] == 'B')) {
        c.stop();
        return -1;
    }
    // WordCount for NT LM 0.12 response sits at offset 32; Capabilities is
    // 19 bytes into the word block (offset 33+19 = 52). CAP_EXTENDED_SECURITY
    // is bit 31.
    uint8_t wc = resp[32];
    if (wc >= 17 && mlen > 56) {
        uint32_t caps = resp[52] | (resp[53] << 8) | (resp[54] << 16) | ((uint32_t)resp[55] << 24);
        if (caps & 0x80000000UL) {
            note = "extended security (null session not tested)";
            c.stop();
            return -1;
        }
    }

    // SESSION_SETUP_ANDX (SMB1, NT non-extended), null credentials.
    uint8_t s[128];
    size_t n = 4;
    s[n++] = 0xFF;
    s[n++] = 'S';
    s[n++] = 'M';
    s[n++] = 'B';
    s[n++] = 0x73;  // SESSION_SETUP_ANDX
    for (int i = 0; i < 4; i++) s[n++] = 0x00;
    s[n++] = 0x18;
    s[n++] = 0x01;
    s[n++] = 0x28;
    for (int i = 0; i < 2; i++) s[n++] = 0x00;   // PIDHigh
    for (int i = 0; i < 8; i++) s[n++] = 0x00;   // signature
    for (int i = 0; i < 2; i++) s[n++] = 0x00;   // reserved
    for (int i = 0; i < 2; i++) s[n++] = 0x00;   // TID
    s[n++] = 0xFF;
    s[n++] = 0xFE;                               // PIDLow
    for (int i = 0; i < 2; i++) s[n++] = 0x00;   // UID
    for (int i = 0; i < 2; i++) s[n++] = 0x00;   // MID
    s[n++] = 13;                                 // WordCount
    s[n++] = 0xFF;                               // AndXCommand = none
    s[n++] = 0x00;                               // AndXReserved
    s[n++] = 0x00;
    s[n++] = 0x00;  // AndXOffset
    s[n++] = 0xFF;
    s[n++] = 0xFF;  // MaxBufferSize
    s[n++] = 0x02;
    s[n++] = 0x00;  // MaxMpxCount
    s[n++] = 0x00;
    s[n++] = 0x00;  // VcNumber
    for (int i = 0; i < 4; i++) s[n++] = 0x00;   // SessionKey
    s[n++] = 0x00;
    s[n++] = 0x00;  // OEM password len = 0
    s[n++] = 0x00;
    s[n++] = 0x00;  // Unicode password len = 0
    for (int i = 0; i < 4; i++) s[n++] = 0x00;   // Reserved
    for (int i = 0; i < 4; i++) s[n++] = 0x00;   // Capabilities
    size_t bcPos = n;
    n += 2;  // ByteCount placeholder
    size_t bcStart = n;
    s[n++] = 0x00;  // AccountName ""  (ASCII, flags2 has no unicode bit)
    s[n++] = 0x00;  // PrimaryDomain ""
    s[n++] = 0x00;  // NativeOS ""
    s[n++] = 0x00;  // NativeLanMan ""
    uint16_t bc = n - bcStart;
    s[bcPos] = bc & 0xFF;
    s[bcPos + 1] = bc >> 8;
    uint32_t slen = n - 4;
    s[0] = 0x00;
    s[1] = (slen >> 16) & 0xFF;
    s[2] = (slen >> 8) & 0xFF;
    s[3] = slen & 0xFF;
    c.write(s, n);

    got = readN(c, resp, 4, kReadTimeoutMs);
    if (got != 4) {
        c.stop();
        return -1;
    }
    mlen = (resp[1] << 16) | (resp[2] << 8) | resp[3];
    if (mlen < 9 || mlen > 250) {
        c.stop();
        return -1;
    }
    int r = readN(c, resp, mlen, kReadTimeoutMs);
    c.stop();
    if (r < 9) return -1;
    // NT status is 4 bytes at SMB header offset 5.
    uint32_t status = resp[5] | (resp[6] << 8) | (resp[7] << 16) | ((uint32_t)resp[8] << 24);
    return (status == 0) ? 1 : 0;
}

}  // namespace

// ---- manager ----------------------------------------------------------

void ServiceAuditManager::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

bool ServiceAuditManager::start(const IPAddress& target) {
    if (_running) return false;
    _target = target;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _findings.clear();
        xSemaphoreGive(_mutex);
    }
    _running = true;
    notify(ScanEventType::ScanStarted);
    xTaskCreatePinnedToCore(&ServiceAuditManager::taskEntry, "svcaudit", 8192, this, 1, nullptr, 0);
    return true;
}

void ServiceAuditManager::taskEntry(void* arg) {
    static_cast<ServiceAuditManager*>(arg)->run();
    vTaskDelete(nullptr);
}

void ServiceAuditManager::run() {
    HostInfo h;
    if (!g_scanManager.getHostByIp(_target, h)) {
        notify("host not found - run NETWORK SCAN + port scan first");
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return;
    }

    bool any = false;
    for (const auto& pr : h.ports) {
        if (!_running) break;
        uint16_t port = pr.port;
        switch (port) {
            case 21: any = true; auditFtp(port); break;
            case 445: any = true; auditSmb(port); break;
            case 6379: any = true; auditRedis(port); break;
            case 3306: any = true; auditMysql(port); break;
            case 5432: any = true; auditPostgres(port); break;
            case 5900: any = true; auditVnc(port); break;
            case 80:
            case 8000:
            case 8080:
            case 8888: any = true; auditHttp(port); break;
            default: break;
        }
    }

    if (!any) notify("no auditable services in open-port list");
    notify(String((unsigned)count()) + " finding(s)");
    _running = false;
    notify(ScanEventType::ScanFinished, 100);
}

void ServiceAuditManager::auditFtp(uint16_t port) {
    notify("ftp: anonymous check...");
    int r = ftpAnonymous(_target, port);
    if (r == 2) addFinding("ftp", "anonymous login ALLOWED (WRITABLE)", true);
    else if (r == 1) addFinding("ftp", "anonymous login ALLOWED", true);
    else if (r == 0) addFinding("ftp", "anonymous denied", false);
}

void ServiceAuditManager::auditSmb(uint16_t port) {
    notify("smb: null session...");
    String note;
    int r = smbNullSession(_target, port, note);
    if (r == 1) addFinding("smb", "anonymous (null) session ACCEPTED", true);
    else if (r == 0) addFinding("smb", "null session denied", false);
    else if (note.length()) addFinding("smb", note, false);
}

void ServiceAuditManager::auditRedis(uint16_t port) {
    notify("redis: auth check...");
    String pass;
    int r = redisCheck(_target, port, pass);
    if (r == 1) addFinding("redis", "NO-AUTH access", true);
    else if (r == 2) addFinding("redis", String("default password '") + pass + "'", true);
    else if (r == 0) addFinding("redis", "auth required (no default pw)", false);
}

void ServiceAuditManager::auditMysql(uint16_t port) {
    notify("mysql: default creds...");
    const char* users[] = {"root", ""};
    const char* passes[] = {"", "root", "password", "toor", "admin", "mysql"};
    String note;
    for (const char* u : users) {
        for (const char* p : passes) {
            if (!_running) return;
            int r = mysqlTry(_target, port, u, p, note);
            if (r == 1) {
                addFinding("mysql", String("valid: ") + (strlen(u) ? u : "(anon)") + ":" + (strlen(p) ? p : "(empty)"),
                           true);
                return;
            }
            if (r == -1 && note.length()) {
                addFinding("mysql", note, false);
                return;
            }
            delay(kAttemptDelayMs);
        }
    }
    addFinding("mysql", "no default creds worked", false);
}

void ServiceAuditManager::auditPostgres(uint16_t port) {
    notify("postgres: default creds...");
    const char* passes[] = {"", "postgres", "password", "admin"};
    for (const char* p : passes) {
        if (!_running) return;
        int r = postgresTry(_target, port, "postgres", p);
        if (r == 1) {
            addFinding("postgres", String("valid: postgres:") + (strlen(p) ? p : "(trust/empty)"), true);
            return;
        }
        if (r == -2) {
            addFinding("postgres", "SCRAM auth (not brute-forced)", false);
            return;
        }
        delay(kAttemptDelayMs);
    }
    addFinding("postgres", "no default creds worked", false);
}

void ServiceAuditManager::auditVnc(uint16_t port) {
    notify("vnc: auth check...");
    int r = vncCheck(_target, port);
    if (r == 1) addFinding("vnc", "NO authentication", true);
    else if (r == 0) addFinding("vnc", "auth required (challenge brute n/a)", false);
}

void ServiceAuditManager::auditHttp(uint16_t port) {
    notify(String("http:") + port + " basic-auth...");
    String cred;
    int r = httpBasic(_target, port, cred);
    if (r == 1) addFinding("http", String("basic-auth ") + cred, true);
    else if (r == 0) addFinding("http", "basic-auth: no default worked", false);
    // r == -1: no basic-auth realm here; nothing to report (form brute out of scope)
}

void ServiceAuditManager::addFinding(const char* service, const String& result, bool critical) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _findings.push_back({String(service), result, critical});
        xSemaphoreGive(_mutex);
    }
    notify(String(critical ? "! " : "") + service + ": " + result);
}

void ServiceAuditManager::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::ServiceAudit;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

void ServiceAuditManager::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::ServiceAudit;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}

size_t ServiceAuditManager::count() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _findings.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool ServiceAuditManager::get(size_t index, Finding& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _findings.size();
    if (ok) out = _findings[index];  // oldest-first: audit order reads naturally
    xSemaphoreGive(_mutex);
    return ok;
}
