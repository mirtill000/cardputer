#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Minimal GNSS receiver driver for the M5Stack Cap LoRa-1262's ATGM336H
// module, which streams standard NMEA-0183 sentences over UART at 9600
// baud (see net/CapLoRa1262.h for the pins). Deliberately self-contained
// — no TinyGPS++/external library — so it adds nothing to the flash/OTA
// budget (see partitions.csv): it parses just the two sentences a
// geotagging use needs, RMC (position + fix validity) and GGA (altitude +
// satellites in use), and ignores everything else.
//
// Runs one low-priority FreeRTOS task that continuously drains the UART RX
// so the driver buffer can't overflow, updating a mutex-protected Fix
// snapshot that other tasks (WardrivingManager) read via current().
//
// No-op-safe when no cap is attached: with nothing driving the RX line the
// task simply never sees a valid sentence, present() stays false and
// current().valid stays false — exactly like SD/RTC degrade to "absent"
// elsewhere in this firmware, so it's always safe to begin() at boot.
class GnssReceiver {
public:
    struct Fix {
        bool valid = false;       // true while RMC reports an active ('A') fix
        double lat = 0.0;         // decimal degrees, + = North, - = South
        double lon = 0.0;         // decimal degrees, + = East,  - = West
        double altitudeM = 0.0;   // metres above mean sea level (from GGA)
        uint8_t satellites = 0;   // satellites in use (from GGA)
        uint32_t lastFixMs = 0;   // millis() when this fix was last refreshed
    };

    void begin();

    // True once the module has sent at least one checksum-valid NMEA
    // sentence — i.e. a cap is attached and talking, even before it has a
    // position lock. Lets the UI tell "no cap" apart from "cap present,
    // still acquiring satellites".
    bool present() const;

    // Raw bytes read off the UART so far, before any parsing. The key
    // diagnostic when present() is false: 0 means nothing is arriving on
    // the RX pin at all (wrong pin / cap absent / not powered), whereas a
    // growing count with present() still false means bytes arrive but
    // aren't valid NMEA (wrong baud, or a non-NMEA stream).
    uint32_t rxBytes() const;

    // Which GPIO the reader is currently listening on. The two documented
    // Cap LoRa-1262 UART pins are auto-probed (see CapLoRa1262.h): the
    // driver starts on one and, if no bytes arrive, switches to the other,
    // so an RX/TX mix-up in the published pin-out fixes itself instead of
    // silently reading a dead pin forever. Exposed so the UI can show
    // which pin ended up carrying the data.
    int activeRxPin() const;

    // A snapshot of the latest fix. .valid is false until the first
    // position lock; the lat/lon are meaningless while invalid.
    Fix current() const;

private:
    static void taskEntry(void* arg);
    void run();
    void handleSentence(char* s);  // one NMEA sentence, NUL-terminated, '$'..pre-CRLF (mutated in place)
    static bool checksumOk(const char* s);

    mutable SemaphoreHandle_t _mutex = nullptr;
    Fix _fix;
    volatile bool _sawData = false;
    volatile uint32_t _rxBytes = 0;
    volatile int _activeRx = -1;
};

extern GnssReceiver g_gnss;
