#include "WordlistLoader.h"
#include <LittleFS.h>

std::vector<String> WordlistLoader::load(const char* path, size_t maxEntries) {
    std::vector<String> out;

    File f = LittleFS.open(path, "r");
    if (!f) {
        log_e("WordlistLoader: could not open %s", path);
        return out;
    }

    // Bounded line reader. readStringUntil('\n') would pull an entire
    // newline-free file (a binary dropped in /creds by mistake) into one
    // String - an out-of-memory crash on this no-PSRAM board. Lines are
    // capped at kMaxLineLen; anything past that on the same line is
    // skipped, and an over-long entry is dropped rather than truncated
    // (a cut-off password would just be a wrong guess).
    constexpr size_t kMaxLineLen = 128;
    char buf[kMaxLineLen + 1];
    size_t len = 0;
    bool overflow = false;
    auto flushLine = [&]() {
        buf[len] = '\0';
        if (!overflow) {
            String line(buf);
            line.trim();  // also strips a trailing '\r' from CRLF-saved files
            if (line.length() && line[0] != '#') out.push_back(line);
        }
        len = 0;
        overflow = false;
    };
    while (f.available() && out.size() < maxEntries) {
        int c = f.read();
        if (c < 0) break;
        if (c == '\n') {
            flushLine();
            continue;
        }
        if (len < kMaxLineLen) buf[len++] = (char)c;
        else overflow = true;
    }
    if ((len || overflow) && out.size() < maxEntries) flushLine();  // last line without a trailing newline

    f.close();
    return out;
}
