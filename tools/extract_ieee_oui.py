#!/usr/bin/env python3
"""
Extracts a simple "OUI,Vendor" CSV from an IEEE MA-L registry text dump
(the format the `netaddr` PyPI package bundles at
netaddr/eui/oui.txt, and the same format IEEE's own oui.txt export
uses) into data/oui/oui.csv, which tools/gen_oui_db.py then turns into
the binary LittleFS asset.

Why go through `netaddr` instead of downloading oui.csv straight from
IEEE (https://standards-oui.ieee.org/oui/oui.csv): that host was not
reachable from the sandbox this firmware was originally developed in.
`netaddr` ships a periodically-refreshed snapshot of the same registry
as an installable package on PyPI, which was reachable, so this script
exists to pull real data out of it instead of hand-typing a vendor list.

The shipped data/oui/oui.csv is a point-in-time snapshot and *will* go
stale as IEEE allocates new blocks. To refresh it:
    pip install --upgrade netaddr
    python3 tools/extract_ieee_oui.py
    python3 tools/gen_oui_db.py tools/oui.csv data/oui/oui.bin
(or, if you have direct access to standards-oui.ieee.org, adapt the
regex below to parse that CSV export instead — it's a different but
similarly simple format.)

Note: the extracted CSV lives under tools/, not data/ — data/ is
flashed to the device's LittleFS as-is (see platformio.ini), and the
~1.1MB source CSV isn't something the firmware ever reads (only the
compact oui.bin built from it is), so it stays out of the flash image.

Usage:
    python3 tools/extract_ieee_oui.py [output_csv]
"""
import csv
import importlib.util
import os
import re
import sys


def find_source_file():
    spec = importlib.util.find_spec("netaddr")
    if spec is None or not spec.submodule_search_locations:
        print("error: the 'netaddr' package is not installed (pip install netaddr)", file=sys.stderr)
        sys.exit(1)
    pkg_dir = spec.submodule_search_locations[0]
    path = os.path.join(pkg_dir, "eui", "oui.txt")
    if not os.path.isfile(path):
        print(f"error: expected {path} to exist, but it doesn't", file=sys.stderr)
        sys.exit(1)
    return path


LINE_PATTERN = re.compile(r"^([0-9A-Fa-f]{2}-[0-9A-Fa-f]{2}-[0-9A-Fa-f]{2})\s+\(hex\)\s+(.+?)\s*$")


def extract(source_path):
    records = {}
    with open(source_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LINE_PATTERN.match(line)
            if not m:
                continue
            oui = m.group(1).replace("-", "").upper()
            vendor = m.group(2).strip()
            if vendor:
                records[oui] = vendor  # later duplicates overwrite earlier ones, matches source order
    return records


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "tools/oui.csv"
    source_path = find_source_file()
    records = extract(source_path)

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "w", newline="", encoding="utf-8") as out:
        w = csv.writer(out)
        for oui in sorted(records):
            w.writerow([oui, records[oui]])

    print(f"extracted {len(records)} OUI records from {source_path} -> {out_path}")


if __name__ == "__main__":
    main()
