#include "OuiDatabase.h"
#include <LittleFS.h>
#include <cstring>

OuiDatabase g_ouiDb;

bool OuiDatabase::begin(const char* path) {
    _mutex = xSemaphoreCreateMutex();

    _file = LittleFS.open(path, "r");
    if (!_file) {
        log_e("OuiDatabase: could not open %s (did you run `pio run -t uploadfs`?)", path);
        return false;
    }

    uint8_t header[kHeaderSize];
    if (_file.read(header, kHeaderSize) != kHeaderSize || memcmp(header, "OUI1", 4) != 0) {
        log_e("OuiDatabase: %s has an unrecognized header", path);
        _file.close();
        return false;
    }

    _count = (uint32_t)header[4] | ((uint32_t)header[5] << 8) | ((uint32_t)header[6] << 16) |
             ((uint32_t)header[7] << 24);
    _ready = (_count > 0);
    return _ready;
}

bool OuiDatabase::lookup(const uint8_t mac[6], String& vendorOut) const {
    if (!_ready) return false;

    // seek()+read() on a single shared File handle isn't atomic, so
    // callers from different scan tasks need to be serialized here.
    // Block until the (tiny) critical section frees rather than giving up
    // after a fixed timeout: under many concurrent scan workers a 250ms
    // cap made lookups fail sporadically (a host silently getting no
    // vendor). No code path holds this mutex across a blocking call, so
    // waiting for it can't deadlock. Guard the handle too, in case
    // begin()'s xSemaphoreCreateMutex() failed at boot.
    if (!_mutex) return false;
    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;

    bool found = false;
    int32_t lo = 0, hi = (int32_t)_count - 1;
    uint8_t buf[kRecordSize];

    while (lo <= hi) {
        int32_t mid = lo + (hi - lo) / 2;
        uint32_t offset = kHeaderSize + (uint32_t)mid * kRecordSize;

        _file.seek(offset);
        if (_file.read(buf, kRecordSize) != kRecordSize) break;

        int cmp = memcmp(buf, mac, 3);
        if (cmp == 0) {
            // Field is truncated+NUL-padded by the generator, so this is
            // always a valid, terminated C string within the 32 bytes.
            vendorOut = String(reinterpret_cast<const char*>(buf + 3));
            found = true;
            break;
        } else if (cmp < 0) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    xSemaphoreGive(_mutex);
    return found;
}
