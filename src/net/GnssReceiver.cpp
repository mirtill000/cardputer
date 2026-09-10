#include "GnssReceiver.h"
#include "CapLoRa1262.h"

#include <HardwareSerial.h>
#include <cstdlib>
#include <cstring>

GnssReceiver g_gnss;

namespace {
HardwareSerial gnssSerial(caplora::kGnssUartNum);

// NMEA latitude/longitude come as ddmm.mmmm (lat) or dddmm.mmmm (lon)
// plus a hemisphere char. Convert to signed decimal degrees. Returns
// false for an empty/malformed field. The floor(v/100) split for the
// degrees works for both widths because minutes are always < 60 < 100.
bool nmeaToDegrees(const char* field, char hemi, double& out) {
    if (!field || !field[0]) return false;
    double v = atof(field);
    double deg = (double)((long)(v / 100.0));
    double minutes = v - deg * 100.0;
    double dec = deg + minutes / 60.0;
    if (hemi == 'S' || hemi == 's' || hemi == 'W' || hemi == 'w') dec = -dec;
    out = dec;
    return true;
}
}  // namespace

void GnssReceiver::begin() {
    _mutex = xSemaphoreCreateMutex();
    // Opens UART1 on the cap's GNSS pins. Harmless if no cap is attached —
    // the RX line just stays idle and nothing is ever parsed.
    gnssSerial.begin(caplora::kGnssBaud, SERIAL_8N1, caplora::kGnssRxPin, caplora::kGnssTxPin);
    xTaskCreatePinnedToCore(&GnssReceiver::taskEntry, "gnss", 4096, this, 1, nullptr, 0);
}

bool GnssReceiver::present() const { return _sawData; }

GnssReceiver::Fix GnssReceiver::current() const {
    Fix out;
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        out = _fix;
        xSemaphoreGive(_mutex);
    }
    return out;
}

void GnssReceiver::taskEntry(void* arg) { static_cast<GnssReceiver*>(arg)->run(); }

void GnssReceiver::run() {
    char line[128];
    size_t len = 0;
    for (;;) {
        while (gnssSerial.available() > 0) {
            char c = (char)gnssSerial.read();
            if (c == '\n' || c == '\r') {
                if (len > 0) {
                    line[len] = '\0';
                    handleSentence(line);
                    len = 0;
                }
            } else if (len < sizeof(line) - 1) {
                line[len++] = c;
            } else {
                len = 0;  // oversized/garbled line: drop it, resync on the next newline
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

bool GnssReceiver::checksumOk(const char* s) {
    // s begins with '$'. NMEA checksum is the XOR of every char between
    // '$' and '*', printed as two hex digits after '*'. Some modules omit
    // the '*<cs>' entirely — accept those (field parsing still bounds-
    // checks everything downstream).
    const char* star = strchr(s, '*');
    if (!star) return true;
    uint8_t sum = 0;
    for (const char* p = s + 1; p < star; p++) sum ^= (uint8_t)*p;
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    int hi = hexVal(star[1]);
    if (hi < 0) return false;
    int lo = hexVal(star[2]);
    if (lo < 0) return false;
    return sum == (uint8_t)((hi << 4) | lo);
}

void GnssReceiver::handleSentence(char* s) {
    if (s[0] != '$') return;
    if (!checksumOk(s)) return;
    _sawData = true;  // a checksum-valid sentence means a cap is attached and talking

    if (char* star = strchr(s, '*')) *star = '\0';  // drop checksum before field split

    // Split on ',' into up to 24 fields (mutating s in place).
    char* fields[24];
    int nf = 0;
    for (char* p = s; nf < 24;) {
        fields[nf++] = p;
        char* comma = strchr(p, ',');
        if (!comma) break;
        *comma = '\0';
        p = comma + 1;
    }
    if (nf < 1) return;

    // Talker id varies (GP/GN/GL/BD/GA...), so match on the last 3 chars
    // of the address field ("$GNGGA" -> "GGA").
    const char* addr = fields[0];
    size_t al = strlen(addr);
    if (al < 6) return;
    const char* typ = addr + al - 3;

    if (strcmp(typ, "RMC") == 0 && nf >= 7) {
        // $..RMC,time,status,lat,N/S,lon,E/W,...
        bool active = (fields[2][0] == 'A');
        double lat = 0.0, lon = 0.0;
        bool okLat = nmeaToDegrees(fields[3], fields[4][0], lat);
        bool okLon = nmeaToDegrees(fields[5], fields[6][0], lon);
        if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            _fix.valid = active && okLat && okLon;
            if (_fix.valid) {
                _fix.lat = lat;
                _fix.lon = lon;
                _fix.lastFixMs = millis();
            }
            xSemaphoreGive(_mutex);
        }
    } else if (strcmp(typ, "GGA") == 0 && nf >= 10) {
        // $..GGA,time,lat,N/S,lon,E/W,fixQual,numSat,HDOP,alt,M,...
        int sats = atoi(fields[7]);
        double alt = atof(fields[9]);
        if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            _fix.satellites = (uint8_t)(sats < 0 ? 0 : (sats > 255 ? 255 : sats));
            _fix.altitudeM = alt;
            xSemaphoreGive(_mutex);
        }
    }
}
