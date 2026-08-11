#include "PortServiceDb.h"
#include <LittleFS.h>
#include <cstring>

PortServiceDb g_portServiceDb;

namespace {
uint32_t sortKey(uint16_t port, uint8_t protoByte) {
    return ((uint32_t)port << 1) | protoByte;
}
}  // namespace

bool PortServiceDb::begin(const char* path) {
    _mutex = xSemaphoreCreateMutex();

    _file = LittleFS.open(path, "r");
    if (!_file) {
        log_e("PortServiceDb: could not open %s (did you run `pio run -t uploadfs`?)", path);
        return false;
    }

    uint8_t header[kHeaderSize];
    if (_file.read(header, kHeaderSize) != kHeaderSize || memcmp(header, "PSV1", 4) != 0) {
        log_e("PortServiceDb: %s has an unrecognized header", path);
        _file.close();
        return false;
    }

    _count = (uint32_t)header[4] | ((uint32_t)header[5] << 8) | ((uint32_t)header[6] << 16) |
             ((uint32_t)header[7] << 24);
    _ready = (_count > 0);
    return _ready;
}

bool PortServiceDb::lookup(uint16_t port, bool udp, String& serviceOut) const {
    if (!_ready) return false;

    // seek()+read() on a single shared File handle isn't atomic, so
    // callers from different scan tasks need to be serialized here —
    // same reasoning as OuiDatabase.
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return false;

    bool found = false;
    uint32_t target = sortKey(port, udp ? 1 : 0);
    int32_t lo = 0, hi = (int32_t)_count - 1;
    uint8_t buf[kRecordSize];

    while (lo <= hi) {
        int32_t mid = lo + (hi - lo) / 2;
        uint32_t offset = kHeaderSize + (uint32_t)mid * kRecordSize;

        _file.seek(offset);
        if (_file.read(buf, kRecordSize) != kRecordSize) break;

        uint16_t recPort = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
        uint32_t recKey = sortKey(recPort, buf[2]);

        if (recKey == target) {
            // Truncated+NUL-padded by the generator, so this is always
            // a valid, terminated C string within the name field.
            serviceOut = String(reinterpret_cast<const char*>(buf + 3));
            found = true;
            break;
        } else if (recKey < target) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    xSemaphoreGive(_mutex);
    return found;
}
