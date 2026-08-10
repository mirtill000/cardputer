#!/usr/bin/env python3
"""
Builds the fixed-record binary OUI database (data/oui/oui.bin) that
src/scan/OuiDatabase.cpp reads directly off LittleFS via seek()s — it
never loads the whole table into RAM, so this scales from a
hand-curated sample up to the full ~35k-entry IEEE registry without
touching the (PSRAM-less) memory budget.

Input: a 2-column CSV, `OUI,Vendor` (OUI = 6 hex digits, no
separators — see tools/oui.csv, produced by
tools/extract_ieee_oui.py).

Output format (all little-endian):
    offset 0: magic b"OUI1" (4 bytes)
    offset 4: uint32 record count
    offset 8: `count` fixed-size records, SORTED by OUI ascending so
              the firmware can binary-search them:
        - 3 bytes: OUI, most-significant octet first (matches the
          first 3 bytes of a MAC address)
        - RECORD_VENDOR_LEN bytes: vendor name, UTF-8, truncated and
          NUL-padded

Usage:
    python3 tools/gen_oui_db.py tools/oui.csv data/oui/oui.bin
"""
import csv
import struct
import sys

RECORD_VENDOR_LEN = 32
RECORD_SIZE = 3 + RECORD_VENDOR_LEN


def parse_rows(path):
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        for row in csv.reader(f):
            if len(row) < 2:
                continue
            oui_hex, vendor = row[0].strip(), row[1].strip()
            if len(oui_hex) != 6:
                continue
            try:
                int(oui_hex, 16)
            except ValueError:
                continue
            if vendor:
                yield oui_hex.upper(), vendor


def main():
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <input.csv> <output.bin>", file=sys.stderr)
        sys.exit(1)

    entries = {}
    for oui_hex, vendor in parse_rows(sys.argv[1]):
        entries[oui_hex] = vendor  # last one wins on duplicate OUI

    records = sorted(entries.items(), key=lambda kv: kv[0])
    if not records:
        print("error: no valid records parsed from input CSV", file=sys.stderr)
        sys.exit(1)

    with open(sys.argv[2], "wb") as out:
        out.write(b"OUI1")
        out.write(struct.pack("<I", len(records)))
        for oui_hex, vendor in records:
            oui_bytes = bytes.fromhex(oui_hex)
            vendor_bytes = vendor.encode("utf-8")[: RECORD_VENDOR_LEN - 1]
            vendor_bytes = vendor_bytes.ljust(RECORD_VENDOR_LEN, b"\x00")
            out.write(oui_bytes)
            out.write(vendor_bytes)

    total_bytes = 8 + len(records) * RECORD_SIZE
    print(f"wrote {len(records)} records ({total_bytes} bytes, {RECORD_SIZE} bytes/record) to {sys.argv[2]}")


if __name__ == "__main__":
    main()
