#include "Base64.h"

namespace {
// Maps one Base64 alphabet character to its 6-bit value, or -1 for
// anything outside the alphabet (including '=', handled separately by
// the caller). Arithmetic on contiguous ASCII ranges rather than a
// 256-entry lookup table - fewer opportunities to mistype an entry.
int decodeChar(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
}  // namespace

std::vector<uint8_t> b64::decode(const String& in) {
    std::vector<uint8_t> out;
    out.reserve((in.length() / 4) * 3);

    int vals[4] = {0, 0, 0, 0};
    int n = 0;
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        if (c == '=') break;
        int v = decodeChar(c);
        if (v < 0) continue;
        vals[n++] = v;
        if (n == 4) {
            out.push_back((uint8_t)((vals[0] << 2) | (vals[1] >> 4)));
            out.push_back((uint8_t)(((vals[1] << 4) | (vals[2] >> 2)) & 0xFF));
            out.push_back((uint8_t)(((vals[2] << 6) | vals[3]) & 0xFF));
            n = 0;
        }
    }
    if (n == 2) {
        out.push_back((uint8_t)((vals[0] << 2) | (vals[1] >> 4)));
    } else if (n == 3) {
        out.push_back((uint8_t)((vals[0] << 2) | (vals[1] >> 4)));
        out.push_back((uint8_t)(((vals[1] << 4) | (vals[2] >> 2)) & 0xFF));
    }
    return out;
}

String b64::encode(const uint8_t* data, size_t len) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    String out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
        out += tbl[(n >> 18) & 0x3F];
        out += tbl[(n >> 12) & 0x3F];
        out += tbl[(n >> 6) & 0x3F];
        out += tbl[n & 0x3F];
        i += 3;
    }

    size_t rem = len - i;
    if (rem == 1) {
        uint32_t n = (uint32_t)data[i] << 16;
        out += tbl[(n >> 18) & 0x3F];
        out += tbl[(n >> 12) & 0x3F];
        out += "==";
    } else if (rem == 2) {
        uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
        out += tbl[(n >> 18) & 0x3F];
        out += tbl[(n >> 12) & 0x3F];
        out += tbl[(n >> 6) & 0x3F];
        out += "=";
    }

    return out;
}
