#!/usr/bin/env python3
"""Select or restore verified Virtua Racing (USA) drive and steering responses.

Drive divisor 225 is original; 180 strengthens acceleration/braking/coasting.
Steering step 32 is original; 48 reaches the unchanged target cap sooner.
Default output remains the original response180 experiment (180/32).

Only four exact, independently tested image hashes are supported. Applying
requires the original image; restoration detects a supported modified image
and reconstructs the exact original. No ROM image or artwork is included.
The source is never written and all output paths must be new files.
"""
import argparse
import hashlib
from pathlib import Path
import struct


ROM_SIZE = 2_097_152
ORIGINAL_SHA256 = "291e74f9b4d5a2c0f2b7cbf6f2e0fbf72c16cb6d83b1f58dda78209f9a64c486"
PATCHED_SHA256 = "c12cb153b7e82854c2e6d23abbe24d981d34a69c5f07a8370fc228bc17738cc3"
PROFILES = {
    (225, 32): ORIGINAL_SHA256,
    (180, 32): PATCHED_SHA256,
    (225, 48): "5c70dafd5dadc84f9a0bcacfd52e75605b67c09c92953cc56e63150be123c4ca",
    (180, 48): "6a2a7b06eef9850a803cb2240a5c28b5ddb188612a16203905fef2609b96d367",
}
# Instructions kept intact by every profile, including the original steering
# target cap, deadbands and averaging. The two signed table entries are data.
GUARDS = {
    0x295CE: bytes.fromhex("83fc"),
    0x2961E: bytes.fromhex("0000"),
    0x29660: bytes.fromhex("43faffbc"),
    0x296AE: bytes.fromhex("0c78007fd000"),
    0x296B6: bytes.fromhex("31fc007fd000"),
    0x296BC: bytes.fromhex("0c78ff81d000"),
    0x296C4: bytes.fromhex("31fcff81d000"),
    0x296D6: bytes.fromhex("0c400020"),
    0x296EC: bytes.fromhex("e282"),
    0x2971E: bytes.fromhex("0c410020"),
}


def profile_edits(drive_divisor=180, steering_step=32):
    """Return only changed words, with original and selected bytes."""
    if (drive_divisor, steering_step) not in PROFILES:
        raise ValueError(f"Unsupported response profile: drive={drive_divisor}, steering={steering_step}")
    # The signed steering pair sums to zero in every profile. Only the speed
    # divisor changes the word sum; transform also recomputes it from bytes.
    header_sum = (0x95AB + drive_divisor - 225) & 0xFFFF
    words = ((0x18E, 0x95AB, header_sum),
             (0x295D0, 225, drive_divisor),
             (0x2961C, (-32) & 0xFFFF, (-steering_step) & 0xFFFF),
             (0x29620, 32, steering_step))
    return tuple((offset, old.to_bytes(2,"big"), new.to_bytes(2,"big"))
                 for offset,old,new in words if old != new)


# Preserve the legacy two-word response180 patch for imported research scripts.
EDITS = profile_edits()


def checksum(data):
    return sum(word[0] for word in struct.iter_unpack(">H", data[0x200:])) & 0xFFFF


def identify_profile(data):
    """Identify a complete supported image; headers/checksums alone never qualify."""
    if len(data) != ROM_SIZE:
        raise ValueError(f"Expected {ROM_SIZE} unheadered ROM bytes; got {len(data)}")
    actual = hashlib.sha256(data).hexdigest()
    profile = next((key for key,value in PROFILES.items() if value == actual), None)
    if profile is None:
        raise ValueError(f"Unsupported input SHA256: {actual}; expected a verified US response profile")
    for offset, expected in GUARDS.items():
        if data[offset:offset+len(expected)] != expected:
            raise ValueError(f"Unexpected original instruction/data at 0x{offset:06X}")
    if int.from_bytes(data[0x18E:0x190], "big") != checksum(data):
        raise ValueError("Source cartridge checksum does not match")
    return profile


def transform(data, restore=False, *, drive_divisor=180, steering_step=32):
    """Apply a selected profile, or automatically restore a recognized variant."""
    edits = profile_edits(drive_divisor, steering_step)
    source_profile = identify_profile(data)
    if restore:
        if (drive_divisor,steering_step) != (180,32):
            raise ValueError("Restore identifies its input profile; omit selection arguments")
        if source_profile == (225,32):
            raise ValueError(f"Unsupported input SHA256: {ORIGINAL_SHA256}; already original")
        edits = profile_edits(*source_profile)
        result_hash = ORIGINAL_SHA256
    else:
        if source_profile != (225,32):
            raise ValueError(f"Unsupported input SHA256: {PROFILES[source_profile]}; applying requires {ORIGINAL_SHA256}")
        result_hash = PROFILES[(drive_divisor,steering_step)]
    result = bytearray(data)
    for offset, original, replacement in edits:
        old, new = (replacement, original) if restore else (original, replacement)
        if data[offset:offset+len(old)] != old:
            raise ValueError(f"Unexpected source word at 0x{offset:06X}")
        result[offset:offset+len(new)] = new
    calculated = checksum(result)
    if int.from_bytes(result[0x18E:0x190],"big") != calculated:
        raise ValueError("Selected profile checksum does not match its edits")
    result[0x18E:0x190] = calculated.to_bytes(2,"big")
    result = bytes(result)
    if hashlib.sha256(result).hexdigest() != result_hash:
        raise ValueError("Unexpected output hash")
    if int.from_bytes(result[0x18E:0x190], "big") != checksum(result):
        raise ValueError("Output cartridge checksum does not match")
    return result


def ips_bytes(restore=False, *, drive_divisor=180, steering_step=32):
    patch = bytearray(b"PATCH")
    for offset, original, replacement in profile_edits(drive_divisor, steering_step):
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
    parser.add_argument("--drive-divisor",type=int,choices=(180,225),
                        help="180: stronger response (default); 225: original drive response")
    parser.add_argument("--steering-step",type=int,choices=(32,48),
                        help="32: original (default); 48: quicker target ramp, unchanged maximum")
    args = parser.parse_args()
    try:
        if args.restore and (args.drive_divisor is not None or args.steering_step is not None):
            raise ValueError("Restore identifies its input profile; omit --drive-divisor and --steering-step")
        if args.source.resolve() == args.output.resolve():
            raise ValueError("Source and output must be different files")
        if args.ips and args.ips.resolve() in (args.source.resolve(), args.output.resolve()):
            raise ValueError("IPS output must have its own path")
        for path in (args.output, args.ips):
            if path and path.exists():
                raise ValueError(f"Refusing to overwrite {path}")
        data = args.source.read_bytes()
        drive = args.drive_divisor if args.drive_divisor is not None else 180
        steering = args.steering_step if args.steering_step is not None else 32
        if args.restore:
            drive,steering = identify_profile(data)
            result = transform(data,True)
        else:
            result = transform(data,drive_divisor=drive,steering_step=steering)
        patch = ips_bytes(args.restore,drive_divisor=drive,steering_step=steering)
        write_new(args.output, result)
        if args.ips:
            write_new(args.ips, patch)
    except (ValueError, OSError) as error:
        parser.exit(2, f"error: {error}\n")
    action = "Restored" if args.restore else "Copied original" if (drive,steering) == (225,32) else "Patched"
    print(action, args.output)
    print("Source profile:" if args.restore else "Output profile:",
          f"drive divisor {drive}, steering step {steering}")
    print("SHA256:", hashlib.sha256(result).hexdigest())
    if args.ips:
        print("IPS:", args.ips, f"({len(patch)} bytes; contains no ROM image)")


if __name__ == "__main__":
    main()
