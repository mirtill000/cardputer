#include "BannerGrabber.h"
#include <Arduino.h>

namespace {

String serviceNameForPort(uint16_t port) {
    switch (port) {
        case 21: return "ftp";
        case 22: return "ssh";
        case 23: return "telnet";
        case 25: return "smtp";
        case 80:
        case 8000:
        case 8080:
        case 8888: return "http";
        case 110: return "pop3";
        case 139: return "netbios-ssn";
        case 143: return "imap";
        case 443: return "https";
        case 445: return "smb";
        case 3389: return "rdp";
        default: return "";
    }
}

bool looksLikeHttp(uint16_t port) {
    return port == 80 || port == 8080 || port == 8000 || port == 8888;
}

// Reads whatever the peer sends within timeoutMs, up to maxLen bytes,
// stopping early at a newline. Best-effort: a service that never sends
// anything unprompted (common — many wait to be spoken to first) just
// yields "".
String readAvailable(WiFiClient& client, uint16_t timeoutMs, size_t maxLen) {
    String out;
    uint32_t start = millis();
    while ((millis() - start) < timeoutMs && out.length() < maxLen) {
        if (client.available()) {
            int c = client.read();
            if (c < 0) break;
            if (c == '\n') break;
            if (c != '\r') out += (char)c;
        } else if (out.length() > 0) {
            // Already got something and the peer paused — good enough,
            // no need to wait out the rest of the timeout.
            break;
        } else {
            delay(5);
        }
    }
    return out;
}

}  // namespace

void BannerGrabber::grab(WiFiClient& client, uint16_t port, uint16_t timeoutMs, PortResult& result) {
    result.service = serviceNameForPort(port);

    if (looksLikeHttp(port)) {
        client.print("HEAD / HTTP/1.0\r\nHost: scan\r\nConnection: close\r\n\r\n");
        result.banner = readAvailable(client, timeoutMs, 96);
        if (result.service.isEmpty()) result.service = "http";
        return;
    }

    if (port == 139 || port == 445) {
        // SMB doesn't hand out a plaintext banner on connect, and a real
        // negotiate-protocol handshake is protocol work out of scope
        // here — open-port evidence alone is still useful (it's already
        // what bumps a host's risk to Warning, see ScanManager::setHostPorts).
        return;
    }

    // Everything else: FTP/SSH/SMTP/Telnet/POP3/IMAP conventionally
    // banner unprompted on connect — just listen for a moment.
    result.banner = readAvailable(client, timeoutMs, 96);
    if (result.service.isEmpty() && result.banner.length()) {
        result.service = "unknown";
    }
}
