#!/usr/bin/env python3
"""Inspect *verified* 32X Virtua Racing Deluxe route-lookup tables, read-only.

The 68000 routine at ROM $0073E8 obtains a word from one of six 64x64
segment-index grids, then adds twice that word to a second ROM pointer.
Those pointers have been cross-checked against the disassembly by
matiaszanolli/sega-vr-disasm (analysis/TRACK_DATA_FORMAT.md). They are
NOT yet decoded track geometry or verified circuit assignments.

No source bytes or game data are output: only anonymous grid statistics.
No donor ROM is modified, copied, or committed to the project.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

ROM_SHA256 = "cc2917895842ba42ce871a07a327c5467a5b5df8b7999ccc4b7b5bea9d4caf6b"
ROM_MD5 = "72b1ad0f949f68da7d0a6339ecd51a3f"
ROM_BYTES = 3145728
M68K_ROM_START = 0x880000
NORMAL_TABLE = 0x742C
ALTERNATE_TABLE = 0x745C
GRID_SIDE = 64
GRID_BYTES = GRID_SIDE * GRID_SIDE * 2
NORMAL_PAIRS = (
    (0x94C000, 0x970000),
    (0x94E000, 0x988000),
    (0x950000, 0x9A0000),
    (0x954000, 0x9B8000),
    (0x958000, 0x9D0000),
    (0x952000, 0x9D0000),
)
ALTERNATE_PAIR = (3, 0x956000, 0x9C4110)


def be16(data, offset):
    if offset < 0 or offset + 2 > len(data):
        raise ValueError(f"16-bit read outside ROM at {offset:#x}")
    return int.from_bytes(data[offset:offset + 2], "big")


def be32(data, offset):
    if offset < 0 or offset + 4 > len(data):
        raise ValueError(f"32-bit read outside ROM at {offset:#x}")
    return int.from_bytes(data[offset:offset + 4], "big")


def translate_pointer(address, length):
    offset = address - M68K_ROM_START
    if offset < 0 or offset >= length:
        raise ValueError(f"unmapped 32X 68000 ROM pointer: {address:#x}")
    return offset


def inspect_slot(data, table_offset, mode):
    """Replay the actual 68K 2-level pointer lookup for 4096 grid cells."""
    offset = table_offset + 8 * mode
    map_ptr, data_ptr = be32(data, offset), be32(data, offset + 4)
    if map_ptr == data_ptr == 0:
        return None
    if not map_ptr or not data_ptr:
        raise ValueError("partially filled lookup pair")
    map_start = translate_pointer(map_ptr, len(data))
    base_start = translate_pointer(data_ptr, len(data))
    if map_start + GRID_BYTES > len(data):
        raise ValueError("segment map extends past ROM")
    indexes = [be16(data, map_start + 2 * i) for i in range(GRID_SIDE ** 2)]
    target_offsets = [base_start + 2 * index for index in indexes]
    for target in target_offsets:
        if target < 0 or target + 2 > len(data):
            raise ValueError(f"tile reference out of ROM at {target:#x}")
    return {
        "mode_slot": mode,
        "lookup_table_offset": table_offset,
        "map_rom_offset": map_start,
        "base_data_rom_offset": base_start,
        "grid_side": GRID_SIDE,
        "grid_cells": len(indexes),
        "distinct_tile_references": len(set(indexes)),
        "offset_min": min(indexes),
        "offset_max": max(indexes),
        "center_tile_reference": indexes[32 * GRID_SIDE + 32],
        "first_tile_reference": indexes[0],
        "last_tile_reference": indexes[-1],
        "lowest_tile_data_address": min(target_offsets),
        "highest_tile_data_address": max(target_offsets),
        "validated_tile_refs": len(target_offsets),
    }


def inspect_image(data, require_identity=True):
    if require_identity:
        if len(data) != ROM_BYTES or hashlib.sha256(data).hexdigest() != ROM_SHA256:
            raise ValueError("32X ROM hash/size does not match the verified user dump")
        if hashlib.md5(data).hexdigest() != ROM_MD5:
            raise ValueError("32X reference disassembly MD5 disagrees with the donor")
        if not data[0x100:0x120].startswith(b"SEGA 32X U") or data[0x3C0:0x3CF] != b"MARS CHECK MODE":
            raise ValueError("32X header or MARS mode header mismatches")
    normal = [inspect_slot(data, NORMAL_TABLE, i) for i in range(6)]
    if require_identity:
        for result, ptrs in zip(normal, NORMAL_PAIRS):
            assert result is not None
            expected = tuple(translate_pointer(x, len(data)) for x in ptrs)
            if (result["map_rom_offset"], result["base_data_rom_offset"]) != expected:
                raise ValueError("normal lookup table differs from audited 68000 code")
    alternative = [inspect_slot(data, ALTERNATE_TABLE, i) for i in range(6)]
    found_alt = [x for x in alternative if x is not None]
    if require_identity:
        if len(found_alt) != 1 or found_alt[0]["mode_slot"] != ALTERNATE_PAIR[0]:
            raise ValueError("alternate lookup slot differs from source disassembly")
        expected_alt = tuple(translate_pointer(x, len(data)) for x in ALTERNATE_PAIR[1:])
        if (found_alt[0]["map_rom_offset"], found_alt[0]["base_data_rom_offset"]) != expected_alt:
            raise ValueError("alternate lookup pointers differ from source disassembly")
    return {
        "status": "rom_lookup_tables_confirmed_not_track_geometry",
        "sha256": hashlib.sha256(data).hexdigest(),
        "source_32x_disassembly": "https://github.com/matiaszanolli/sega-vr-disasm/blob/master/analysis/TRACK_DATA_FORMAT.md",
        "normal": normal,
        "alternate": found_alt,
        "track_meshes_imported": False,
        "course_order_verified": False,
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("rom", type=Path, help="Unmodified US Virtua Racing Deluxe 32X ROM")
    p.add_argument("--output", type=Path, help="Optional JSON statistics file; no asset data")
    args = p.parse_args()
    report = inspect_image(args.rom.read_bytes())
    content = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(content)
    else:
        print(content, end="")


if __name__ == "__main__":
    main()
