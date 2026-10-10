#!/usr/bin/env python3
"""Check release identity, embedded update source and ZIP contents; print SHA-256."""
import argparse
import hashlib
import struct
import zipfile
from pathlib import Path

OFFICIAL_REPO = "stevenjc2009-byte/noctopage"


def check(path, expected_repo=OFFICIAL_REPO):
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) != len(set(n.lower() for n in names)):
            raise ValueError("Duplicate archive filenames")
        for name in names:
            if name.startswith("/") or "\\" in name or ":" in name or any(
                part in (".", "..") for part in name.rstrip("/").split("/")
            ):
                raise ValueError("Unsafe archive path: " + name)
        bad = archive.testzip()
        if bad:
            raise ValueError("Invalid CRC: " + bad)
        sfo = archive.read("sce_sys/param.sfo")
        magic, _, keys, values, count = struct.unpack_from("<5I", sfo)
        if magic != 0x46535000:
            raise ValueError("Invalid param.sfo magic")
        fields = {}
        for i in range(count):
            keyoff, fmt, length, maximum, offset = struct.unpack_from("<HHIII", sfo, 20 + i * 16)
            key = sfo[keys + keyoff:].split(b"\0", 1)[0].decode()
            fields[key] = sfo[values + offset:values + offset + length].rstrip(b"\0")
        if fields.get("TITLE_ID") != b"NOCT00001":
            raise ValueError("Incorrect app title ID")
        version = fields["APP_VER"].decode()
        if len(version) != 5 or version[2] != "." or not version.replace(".", "").isdigit():
            raise ValueError("Incorrect Vita app version")
        if expected_repo.encode() not in archive.read("eboot.bin"):
            raise ValueError("Expected GitHub update source is absent from eboot.bin")
        if len(archive.read("sce_sys/package/head.bin")) != 1072:
            raise ValueError("Missing/incorrect installation header")
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    print(f"PASS: NoctoPage {version}; updater {expected_repo}; ZIP/CRC/identity/header validated")
    print(f"{digest}  {path.name}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vpk", type=Path)
    parser.add_argument("--repository", default=OFFICIAL_REPO)
    args = parser.parse_args()
    check(args.vpk, args.repository)
