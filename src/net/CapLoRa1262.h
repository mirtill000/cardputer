#pragma once

#include <cstdint>

// Pin map for the M5Stack "Cap LoRa-1262" expansion on the Cardputer ADV
// (product U214): an SX1262 LoRa radio (SPI) plus an ATGM336H GNSS
// receiver (UART NMEA). One place for every GPIO the cap uses, documented
// with its source, so wiring lives here and only here.
//
// Values are the published Cardputer-ADV pin assignment for this cap
// (M5Stack docs / distributor spec sheets, Sept 2026):
//   docs.m5stack.com/en/cap/Cap_LoRa-1262
//   docs.m5stack.com/en/arduino/projects/cap/cap_lora868
// If a future board revision remaps them, this header is the only edit.
namespace caplora {

// --- GNSS (ATGM336H) : UART, standard NMEA-0183 @ 9600 baud ---------
// Pin names are from the Cardputer-ADV (host MCU) perspective: the MCU's
// RX receives the module's NMEA stream. TX is only needed to send the
// module configuration commands, which this firmware does not do.
constexpr int      kGnssRxPin = 15;   // MCU RX  <- module TX (NMEA in)
constexpr int      kGnssTxPin = 13;   // MCU TX  -> module RX
constexpr uint32_t kGnssBaud  = 9600;
constexpr int      kGnssUartNum = 1;  // ESP32-S3 UART1 (UART0 is the USB/serial log)

// --- SX1262 (LoRa) : SPI + control lines ----------------------------
// Wired here for completeness and a future LoRa feature (telemetry of the
// wardrive log, a LoRa/Meshtastic RF survey, ...). NOT used by any code
// yet — adding LoRa means pulling in a radio library (e.g. RadioLib) and
// checking the flash/OTA budget in partitions.csv first.
constexpr int kLoraNssPin  = 5;    // SPI chip select (NSS)
constexpr int kLoraMosiPin = 14;
constexpr int kLoraMisoPin = 39;
constexpr int kLoraSckPin  = 40;
constexpr int kLoraDio1Pin = 4;    // IRQ / DIO1
constexpr int kLoraRstPin  = 3;
constexpr int kLoraBusyPin = 6;

}  // namespace caplora
