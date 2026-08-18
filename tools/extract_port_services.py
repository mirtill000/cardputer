#!/usr/bin/env python3
"""
Extracts a simple "port,proto,name" CSV (tools/port_services.csv) from a
local nmap-services-format file, keeping ONLY the bare port/protocol/
service-name facts and discarding everything else.

Why not just ship nmap-services itself: that file is (C) Insecure.Com
LLC / The Nmap Project and distributed under the Nmap Public Source
License, whose text explicitly states that an application "reads or
includes copyrighted data files, such as Nmap's nmap-os-db or
nmap-service-probes" counts as a derivative work for licensing
purposes — i.e. embedding that file as-is would pull this firmware
under nmap's GPL-2-with-clarifications terms, a licensing decision
this script deliberately avoids making on the user's behalf.

What this script actually takes from that file: the bare fact
"port N/proto is commonly known as service X" — which is not, by
itself, nmap's creative work; it mirrors the same public assignments
IANA maintains in its Service Name and Transport Protocol Port Number
Registry (https://www.iana.org/assignments/service-names-port-numbers/).
Nmap's specific contribution — real-world open-frequency percentages,
per-port research comments, RFC citations, file structure — is exactly
what gets discarded here. The result is a plain fact list in our own
format, not a copy of theirs.

Usage:
    python3 tools/extract_port_services.py [/usr/share/nmap/nmap-services] [tools/port_services.csv]

The source file is NOT committed to this repo (only the extracted
tools/port_services.csv is) — install nmap locally (`apt install nmap`
/ `brew install nmap`) to get a copy at the default path if you want to
regenerate or refresh this list.
"""
import csv
import sys


def extract(source_path):
    seen = {}
    with open(source_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            fields = line.rstrip("\n").split("\t")
            if len(fields) < 2:
                continue
            name, portproto = fields[0], fields[1]
            if name == "unknown" or "/" not in portproto:
                continue
            port_str, proto = portproto.split("/", 1)
            if proto not in ("tcp", "udp"):
                continue  # skips /sctp and anything else — this firmware only ever speaks TCP/UDP
            try:
                port = int(port_str)
            except ValueError:
                continue
            if not (0 <= port <= 65535):
                continue
            seen[(port, proto)] = name  # last one wins on a duplicate (file is already unique per port/proto though)
    return seen


def main():
    source_path = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/nmap/nmap-services"
    out_path = sys.argv[2] if len(sys.argv) > 2 else "tools/port_services.csv"

    records = extract(source_path)
    rows = sorted(records.items(), key=lambda kv: (kv[0][0], kv[0][1]))

    with open(out_path, "w", newline="", encoding="utf-8") as out:
        w = csv.writer(out)
        for (port, proto), name in rows:
            w.writerow([port, proto, name])

    print(f"extracted {len(rows)} port/service facts from {source_path} -> {out_path}")


if __name__ == "__main__":
    main()
