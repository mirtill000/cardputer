#pragma once

#include <cstddef>
#include <cstdint>

// Ports above the classic 1-1024 "well-known" range that are common
// enough in practice (databases, admin panels, dev/HTTP-alt servers,
// remote-access services) that a plain 1-1024 sweep misses them
// entirely — MySQL, Postgres, Redis, MongoDB, RDP, VNC, WinRM,
// Elasticsearch, 8080/3000/9000-style dev servers, and the odd
// exotic-but-real one (31337). Appended to every PORT SCAN alongside
// whatever range is configured in SETTINGS — see
// PortScanManager::startScan, which deduplicates against that range so
// raising portRangeEnd past 1024 doesn't double-probe anything here.
inline constexpr uint16_t kWellKnownHighPorts[] = {
    1080,  1433,  1521,  2049,  2181,  3000,  3128,  3306,  3389,  3690,
    4443,  4444,  5000,  5432,  5601,  5672,  5900,  5985,  5986,  6379,
    6443,  7001,  8000,  8008,  8080,  8081,  8088,  8089,  8090,  8161,
    8443,  8500,  8888,  8983,  9000,  9042,  9090,  9092,  9200,  9300,
    9999,  10000, 11211, 15672, 27017, 27018, 28017, 31337, 32400, 50000,
};
inline constexpr size_t kWellKnownHighPortsCount = sizeof(kWellKnownHighPorts) / sizeof(kWellKnownHighPorts[0]);
