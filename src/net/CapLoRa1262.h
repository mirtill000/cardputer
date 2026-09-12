#pragma once

#include <cstdint>

// Pin map for the M5Stack "Cap LoRa-1262" expansion on the Cardputer ADV
// (product U214): an SX1262 LoRa radio (SPI) plus an ATGM336H GNSS
// receiver (UART NMEA), with a PI4IOE5V6408 I2C IO-expander that drives
// the LoRa RF antenna switch. One place for every GPIO the cap uses.
//
// Values are the OFFICIAL M5Stack pin map (docs.m5stack.com Cap-Bus table
// for Cardputer-Adv, confirmed Sept 2026), not an inference.
namespace caplora {

// --- GNSS (ATGM336H) : UART, standard NMEA-0183 @ 9600 baud ---------
// The doc labels the pins from the MODULE's side: "GPS-TX" is the GNSS
// module's transmit line (the NMEA stream), on Cardputer G15; "GPS-RX"
// is the module's receive line, on G13. So from the MCU's side the RX
// pin — the one we must read NMEA on — is G15, and the MCU TX is G13.
constexpr int      kGnssRxPin = 15;   // MCU RX  <- module "GPS-TX"  (NMEA in)  [doc: G15]
constexpr int      kGnssTxPin = 13;   // MCU TX  -> module "GPS-RX"             [doc: G13]
constexpr uint32_t kGnssBaud  = 9600;
constexpr int      kGnssUartNum = 1;  // ESP32-S3 UART1 (UART0 is the USB/serial log)

// --- SX1262 (LoRa) : SPI + control lines ----------------------------
// Wired here for a future LoRa feature; not used by any code yet. Note
// the RF antenna switch must be enabled before the SX1262 will transmit/
// receive — see the expander section below.
constexpr int kLoraNssPin  = 5;    // SPI chip select (NSS)   [doc: G5]
constexpr int kLoraMosiPin = 14;   //                          [doc: G14]
constexpr int kLoraMisoPin = 39;   //                          [doc: G39]
constexpr int kLoraSckPin  = 40;   //                          [doc: G40]
constexpr int kLoraDio1Pin = 4;    // IRQ / DIO1               [doc: G4]
constexpr int kLoraRstPin  = 3;    //                          [doc: G3]
constexpr int kLoraBusyPin = 6;    //                          [doc: G6]

// --- PI4IOE5V6408 I2C IO-expander -----------------------------------
// On the cap's I2C bus (shared Grove/HY2.0-4P). Its P0 output drives the
// LoRa RF antenna switch (FM8625H / SX_ANT_SW): the M5 docs say P0 must
// be set HIGH to initialise the LoRa module. NOT required for the GNSS,
// which is powered directly. Documented here for when LoRa gets wired up.
constexpr int     kExpanderSdaPin = 8;    // [doc: G8]
constexpr int     kExpanderSclPin = 9;    // [doc: G9]
constexpr uint8_t kExpanderI2cAddr = 0x43;  // PI4IOE5V6408 default 7-bit address
constexpr uint8_t kExpanderAntSwitchBit = 0;  // P0 = SX_ANT_SW (set HIGH to enable LoRa RF)

}  // namespace caplora
