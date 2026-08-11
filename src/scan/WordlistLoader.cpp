#include "WordlistLoader.h"
#include <LittleFS.h>

std::vector<String> WordlistLoader::load(const char* path, size_t maxEntries) {
    std::vector<String> out;

    File f = LittleFS.open(path, "r");
    if (!f) {
        log_e("WordlistLoader: could not open %s", path);
        return out;
    }

    while (f.available() && out.size() < maxEntries) {
        String line = f.readStringUntil('\n');
        line.trim();  // also strips a trailing '\r' from CRLF-saved files
        if (line.length() == 0 || line[0] == '#') continue;
        out.push_back(line);
    }

    f.close();
    return out;
}
