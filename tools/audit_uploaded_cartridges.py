#!/usr/bin/env python3
"""Read-only ROM identity and integrity audit: Race Drivin' Genesis and VR Deluxe 32X.

Never exports donor data, guesses asset offsets, or mutates input files.
Whole-file hashes are verified separately from original 16-bit Sega checksum.
"""
import argparse
import hashlib
import json
from pathlib import Path

KNOWN = {
    "bd86c32fba134c64c9698d939b9d679ad93b77a2864019b63effad1be96002e5":
        ("race_drivin_genesis", 524288, "SEGA GENESIS", "GM T-48246 -00"),
    "cc2917895842ba42ce871a07a327c5467a5b5df8b7999ccc4b7b5bea9d4caf6b":
        ("virtua_racing_deluxe_32x", 3145728, "SEGA 32X", "GM MK-84601-00"),
}

def header_text(data):
    return data.decode("ascii", errors="replace").replace("\x00", " ").strip()

def inspect(path, require_known=False):
    image = Path(path).read_bytes()
    if len(image) < 512 or len(image) & 1:
        raise ValueError("Expected an even-byte Sega ROM with a full 512-byte header")
    system = header_text(image[0x100:0x120])
    if not system.startswith(("SEGA GENESIS", "SEGA 32X")):
        raise ValueError("Unknown cartridge byte ordering or platform")
    checksum = int.from_bytes(image[0x18e:0x190], "big")
    calculated = sum((image[i] << 8) | image[i+1]
                     for i in range(0x200, len(image), 2)) & 0xffff
    if checksum != calculated:
        raise ValueError("Sega checksum mismatch; reject mutated ROM")
    pc = int.from_bytes(image[4:8], "big")
    if pc < 0x200 or pc >= len(image) or pc & 1:
        raise ValueError("Invalid 68000 reset vector")
    digest = hashlib.sha256(image).hexdigest()
    known = KNOWN.get(digest)
    product = header_text(image[0x180:0x18e])
    if require_known and not known:
        raise ValueError("Unknown full-file SHA256; do not assume known offsets")
    if known and (len(image) != known[1] or not system.startswith(known[2]) or
                  product != known[3]):
        raise ValueError("Revision header contradicts verified whole-file hash")
    return {
        "sha256": digest, "bytes": len(image),
        "known_revision": known is not None,
        "family": known[0] if known else "unclassified_sega_rom",
        "system": system, "title": header_text(image[0x150:0x180]),
        "product": product, "region": header_text(image[0x1f0:0x200]),
        "checksum": f"{checksum:04x}", "reset_68000_pc": f"{pc:06x}",
        "original_physics_code_recovered": False,
        "deluxe_courses_imported": False,
    }

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", nargs="+", type=Path)
    parser.add_argument("--require-known", action="store_true")
    args = parser.parse_args()
    print(json.dumps([inspect(p, args.require_known) for p in args.rom],
                     indent=2, sort_keys=True))

if __name__ == "__main__":
    main()
