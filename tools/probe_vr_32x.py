#!/usr/bin/env python3
"""Read-only identification of a user-provided Sega 32X Virtua Racing cart.

No data export, no known offsets, no automatic use of Mega Drive/SVP tables.
Never writes the input or builds a derived ROM. This is not a 32X importer.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys


def ascii_header(raw):
    return raw.decode("ascii", errors="replace").replace("\x00", " ").strip()


def inspect(path, required_hash=None):
    image = Path(path).read_bytes()
    if len(image) < 0x200:
        raise ValueError("Too small for a Sega cartridge header (minimum 512 bytes)")
    digest = hashlib.sha256(image).hexdigest()
    if required_hash is not None and digest.lower() != required_hash.lower():
        raise ValueError("SHA-256 does not match expected original revision; no import attempted")
    system = ascii_header(image[0x100:0x120])
    if "SEGA" not in system.upper() or "32X" not in system.upper():
        raise ValueError("Not a recognized byte-ordered Sega 32X header; check ROM format/revision")
    return {
        "status": "32x_header_identified_only",
        "source_sha256": digest,
        "bytes": len(image),
        "system": system,
        "domestic_title": ascii_header(image[0x120:0x150]),
        "international_title": ascii_header(image[0x150:0x180]),
        "product": ascii_header(image[0x180:0x18E]),
        "region": ascii_header(image[0x1F0:0x200]),
        "course_import_supported": False,
        "physics_import_supported": False,
        "note": "Metadata only. No geometry offsets or original routines have been verified.",
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path, help="Your unmodified Sega 32X ROM")
    parser.add_argument("--sha256", metavar="DIGEST", help="Require this exact source hash")
    args = parser.parse_args(argv)
    try:
        info = inspect(args.rom, args.sha256)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(json.dumps(info, indent=2, ensure_ascii=True, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
