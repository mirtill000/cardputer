#include "PcapWriter.h"
#include <Arduino.h>
#include <cstring>

void pcap::writeGlobalHeader(File& f) {
    uint8_t hdr[24] = {
        0xD4, 0xC3, 0xB2, 0xA1,  // magic (LE reader marker)
        0x02, 0x00,              // version major = 2
        0x04, 0x00,              // version minor = 4
        0x00, 0x00, 0x00, 0x00,  // thiszone
        0x00, 0x00, 0x00, 0x00,  // sigfigs
        0xFF, 0xFF, 0x00, 0x00,  // snaplen = 65535
        0x69, 0x00, 0x00, 0x00,  // network = 105 (IEEE 802.11)
    };
    f.write(hdr, sizeof(hdr));
}

void pcap::writeRecord(File& f, const uint8_t* data, uint16_t capturedLen, uint16_t originalLen) {
    uint32_t sec = (uint32_t)(millis() / 1000);
    uint32_t usec = (uint32_t)((millis() % 1000) * 1000);
    uint8_t hdr[16];
    memcpy(hdr + 0, &sec, 4);
    memcpy(hdr + 4, &usec, 4);
    uint32_t incl = capturedLen, orig = originalLen;
    memcpy(hdr + 8, &incl, 4);
    memcpy(hdr + 12, &orig, 4);
    f.write(hdr, sizeof(hdr));
    f.write(data, capturedLen);
}
