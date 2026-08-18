#!/usr/bin/env python3
"""
Builds the fixed-record binary port/service database
(data/ports/services.bin) that src/scan/PortServiceDb.cpp reads
directly off LittleFS via seek()s — same design as
tools/gen_oui_db.py's OUI database, for the same reason: don't load a
~300KB table into RAM on a board with no PSRAM when a binary search
over on-flash records does the job.

Input: tools/port_services.csv (port,proto,name), produced by
tools/extract_port_services.py — see that script for why this is an
independently-extracted fact list rather than a copy of nmap-services.

Output format (all little-endian):
    offset 0: magic b"PSV1" (4 bytes)
    offset 4: uint32 record count
    offset 8: `count` fixed-size records, SORTED ascending by a
              combined sort key (port << 1 | proto_bit, proto_bit:
              0=tcp, 1=udp) so the firmware can binary-search by the
              same key:
        - 2 bytes: port, little-endian
        - 1 byte:  protocol (0 = tcp, 1 = udp)
        - NAME_FIELD_LEN bytes: service name, UTF-8, NUL-padded/
          truncated (21 chars covers every name in the current CSV
          with room to spare; only bites if a future refresh adds a
          longer one)

Usage:
    python3 tools/gen_port_services_db.py tools/port_services.csv data/ports/services.bin
"""
import csv
import struct
import sys

NAME_FIELD_LEN = 22
RECORD_SIZE = 2 + 1 + NAME_FIELD_LEN


def parse_rows(path):
    with open(path, newline="", encoding="utf-8") as f:
        for row in csv.reader(f):
            if len(row) < 3:
                continue
            port_str, proto, name = row[0].strip(), row[1].strip(), row[2].strip()
            if proto not in ("tcp", "udp") or not name:
                continue
            try:
                port = int(port_str)
            except ValueError:
                continue
            if not (0 <= port <= 65535):
                continue
            yield port, proto, name


def sort_key(port, proto):
    return (port << 1) | (1 if proto == "udp" else 0)


def main():
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <input.csv> <output.bin>", file=sys.stderr)
        sys.exit(1)

    entries = {}
    for port, proto, name in parse_rows(sys.argv[1]):
        entries[(port, proto)] = name  # last one wins on a duplicate

    records = sorted(entries.items(), key=lambda kv: sort_key(kv[0][0], kv[0][1]))
    if not records:
        print("error: no valid records parsed from input CSV", file=sys.stderr)
        sys.exit(1)

    with open(sys.argv[2], "wb") as out:
        out.write(b"PSV1")
        out.write(struct.pack("<I", len(records)))
        for (port, proto), name in records:
            out.write(struct.pack("<H", port))
            out.write(bytes([1 if proto == "udp" else 0]))
            name_bytes = name.encode("utf-8")[: NAME_FIELD_LEN - 1]
            name_bytes = name_bytes.ljust(NAME_FIELD_LEN, b"\x00")
            out.write(name_bytes)

    total_bytes = 8 + len(records) * RECORD_SIZE
    print(f"wrote {len(records)} records ({total_bytes} bytes, {RECORD_SIZE} bytes/record) to {sys.argv[2]}")


if __name__ == "__main__":
    main()
