#!/usr/bin/env python3
"""Apply or restore an experimental Virtua Racing (USA) longitudinal response patch.

Changes the original 68000 speed-integration divisor from 225 to 180 and
updates the cartridge checksum. Both acceleration and braking/coasting
response change, subject to the original integer arithmetic and clamps.

This is a research experiment, not an engine rewrite or a new speed cap.
It contains no ROM image or extracted artwork. Only the exact supported
unmodified image is accepted for applying; restoring accepts the exact
patched image. The source is never written and outputs must be new files.
"""
import argparse
import hashlib
from pathlib import Path
import struct


ROM_SIZE = 2_097_152
ORIGINAL_SHA256 = "291e74f9b4d5a2c0f2b7cbf6f2e0fbf72c16cb6d83b1f58dda78209f9a64c486"
PATCHED_SHA256 = "c12cb153b7e82854c2e6d23abbe24d981d34a69c5f07a8370fc228bc17738cc3"
# Offset, original word, replacement word. Only two bytes differ in the image:
# low byte of DIVS immediate, and low byte of the header checksum.
EDITS = ((0x18E, b"\x95\xab", b"\x95\x7e"),
         (0x295D0, b"\x00\xe1", b"\x00\xb4"))


def checksum(data):
    return sum(word[0] for word in struct.iter_unpack(">H", data[0x200:])) & 0xFFFF


def transform(data, restore=False):
    expected = PATCHED_SHA256 if restore else ORIGINAL_SHA256
    result_hash = ORIGINAL_SHA256 if restore else PATCHED_SHA256
    if len(data) != ROM_SIZE:
        raise ValueError(f"Expected {ROM_SIZE} unheadered ROM bytes; got {len(data)}")
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected:
        raise ValueError(f"Unsupported input SHA256: {actual}; expected {expected}")
    if data[0x295CE:0x295D0] != b"\x83\xfc":
        raise ValueError("Expected DIVS.W immediate instruction at 0x0295CE")
    if int.from_bytes(data[0x18E:0x190], "big") != checksum(data):
        raise ValueError("Source cartridge checksum does not match")
    result = bytearray(data)
    for offset, original, replacement in EDITS:
        old, new = (replacement, original) if restore else (original, replacement)
        if data[offset:offset+len(old)] != old:
            raise ValueError(f"Unexpected source word at 0x{offset:06X}")
        result[offset:offset+len(new)] = new
    result = bytes(result)
    if hashlib.sha256(result).hexdigest() != result_hash:
        raise ValueError("Unexpected output hash")
    if int.from_bytes(result[0x18E:0x190], "big") != checksum(result):
        raise ValueError("Output cartridge checksum does not match")
    return result


def ips_bytes(restore=False):
    patch = bytearray(b"PATCH")
    for offset, original, replacement in EDITS:
        value = original if restore else replacement
        patch += offset.to_bytes(3, "big") + len(value).to_bytes(2, "big") + value
    return bytes(patch + b"EOF")


def write_new(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--restore", action="store_true", help="Reconstruct the exact original ROM")
    parser.add_argument("--ips", type=Path, help="Also write a small IPS patch; use this script for hash checking")
    args = parser.parse_args()
    try:
        if args.source.resolve() == args.output.resolve():
            raise ValueError("Source and output must be different files")
        if args.ips and args.ips.resolve() in (args.source.resolve(), args.output.resolve()):
            raise ValueError("IPS output must have its own path")
        for path in (args.output, args.ips):
            if path and path.exists():
                raise ValueError(f"Refusing to overwrite {path}")
        result = transform(args.source.read_bytes(), args.restore)
        write_new(args.output, result)
        if args.ips:
            write_new(args.ips, ips_bytes(args.restore))
    except (ValueError, OSError) as error:
        parser.exit(2, f"error: {error}\n")
    print("Restored" if args.restore else "Patched", args.output)
    print("SHA256:", hashlib.sha256(result).hexdigest())
    if args.ips:
        print("IPS:", args.ips, "(22 bytes; contains no ROM image)")


if __name__ == "__main__":
    main()
