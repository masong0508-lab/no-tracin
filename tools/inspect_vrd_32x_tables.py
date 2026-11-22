#!/usr/bin/env python3
"""Inspect authenticated Virtua Racing Deluxe 32X road-lookup data, read-only.

The 68K code and track data use DIFFERENT 32X cartridge mapping windows:
  code at 0x8873E8 -> file 0x0073E8 (fixed 0x880000 window)
  data at 0x94C000 -> file 0x14C000 (0x800000 data mirror)
The earlier attempt at subtracting 0x880000 from 0x9xxxxx DATA pointers was
incorrect and produced spurious, random-looking tile counts. This module
rejects that mapping explicitly. It decodes the real sparse lookup layouts,
not 3D geometry, path ordering or complete playable racing courses.

All measurements use the user's own ROM without copying/changing its bytes.
"""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

ROM_SHA256 = "cc2917895842ba42ce871a07a327c5467a5b5df8b7999ccc4b7b5bea9d4caf6b"
ROM_MD5 = "72b1ad0f949f68da7d0a6339ecd51a3f"
ROM_BYTES = 3145728
NORMAL_TABLE = 0x742C
ALTERNATE_TABLE = 0x745C
GRID_SIDE = 64
GRID_BYTES = GRID_SIDE * GRID_SIDE * 2
DATA_WINDOW_START, DATA_WINDOW_END = 0x900000, 0xA00000
DATA_FILE_BASE = 0x800000
NORMAL_PAIRS = (
    (0x94C000, 0x970000),
    (0x94E000, 0x988000),
    (0x950000, 0x9A0000),
    (0x954000, 0x9B8000),
    (0x958000, 0x9D0000),
    (0x952000, 0x9D0000),
)
ALTERNATE_PAIR = (3, 0x956000, 0x9C4110)
# Three auxiliary paged-data bases, read by scene_camera_init's C268 pointer.
# These are NOT assumed to be the five course identities.
AUXILIARY_PAGE_TABLE = 0x130612
AUXILIARY_PAGE_POINTERS = (0x93061E, 0x93261E, 0x93461E)


def be16(data, offset):
    if offset < 0 or offset + 2 > len(data):
        raise ValueError(f"16-bit read outside ROM at {offset:#x}")
    return int.from_bytes(data[offset:offset + 2], "big")


def be32(data, offset):
    if offset < 0 or offset + 4 > len(data):
        raise ValueError(f"32-bit read outside ROM at {offset:#x}")
    return int.from_bytes(data[offset:offset + 4], "big")


def translate_pointer(address, length):
    """Resolve only the 0x90-0x9F data window, NOT the separate code window."""
    if not DATA_WINDOW_START <= address < DATA_WINDOW_END:
        raise ValueError(f"unmapped 32X data address: {address:#x}")
    offset = address - DATA_FILE_BASE
    if offset >= length:
        raise ValueError(f"32X data pointer out of ROM: {address:#x}")
    return offset


def coordinate_grid_index(x, z):
    """Replay 68000 $73E8 map addressing in its valid 64x64 world window.

    The 68K uses ASR.W on 16-bit world X/Z, then aligns the row to 32 and
    doubles it twice. Negative out-of-map values would wrap under 68000
    16-bit arithmetic, so reject them instead of aliasing unrelated tiles.
    """
    if not (-16384 <= x < 16384 and -16384 <= z < 16384):
        raise ValueError("coordinate outside the 64x64 lookup grid")
    col = (0x400 + (x >> 4)) >> 5
    aligned_row = (0x400 + (z >> 4)) & 0xffe0
    word_index = aligned_row * 2 + col
    if not 0 <= word_index < GRID_SIDE * GRID_SIDE:
        raise ValueError("coordinate outside the 64x64 lookup grid")
    return word_index


def _components(indices):
    """Sizes of 4-connected non-dominant tile groups, no data export."""
    remaining = set(indices)
    sizes = []
    while remaining:
        seed = remaining.pop()
        queue = [seed]
        count = 0
        while queue:
            v = queue.pop()
            count += 1
            row, col = divmod(v, GRID_SIDE)
            for w in (v - GRID_SIDE if row else -1,
                      v + GRID_SIDE if row + 1 < GRID_SIDE else -1,
                      v - 1 if col else -1,
                      v + 1 if col + 1 < GRID_SIDE else -1):
                if w in remaining:
                    remaining.remove(w)
                    queue.append(w)
        sizes.append(count)
    return sorted(sizes, reverse=True)


def inspect_slot(data, table_offset, mode):
    """Read 4096 actual grid references with the correct physical ROM mapping."""
    if not 0 <= mode < 6:
        raise ValueError("invalid 32X lookup mode slot")
    offset = table_offset + 8 * mode
    map_ptr, data_ptr = be32(data, offset), be32(data, offset + 4)
    if map_ptr == data_ptr == 0:
        return None
    if not map_ptr or not data_ptr:
        raise ValueError("partially filled lookup pair")
    map_start = translate_pointer(map_ptr, len(data))
    base_start = translate_pointer(data_ptr, len(data))
    if map_start + GRID_BYTES > len(data):
        raise ValueError("32X lookup map extends past ROM")
    indexes = [be16(data, map_start + 2 * i) for i in range(GRID_SIDE**2)]
    target_offsets = [base_start + 2 * index for index in indexes]
    for target in target_offsets:
        if target < 0 or target + 2 > len(data):
            raise ValueError(f"tile reference out of ROM at {target:#x}")
    dominant_index, dominant_count = Counter(indexes).most_common(1)[0]
    active = [i for i, index in enumerate(indexes) if index != dominant_index]
    components = _components(active)
    coords = [(i // GRID_SIDE, i % GRID_SIDE) for i in active]
    bbox = {
        "min_row": min((r for r, _ in coords), default=None),
        "max_row": max((r for r, _ in coords), default=None),
        "min_column": min((c for _, c in coords), default=None),
        "max_column": max((c for _, c in coords), default=None),
    }
    return {
        "mode_slot": mode,
        "lookup_table_offset": table_offset,
        "map_rom_offset": map_start,
        "base_data_rom_offset": base_start,
        "grid_side": GRID_SIDE,
        "grid_cells": len(indexes),
        "distinct_tile_references": len(set(indexes)),
        "dominant_index": dominant_index,
        "dominant_index_cells": dominant_count,
        "non_dominant_cells": len(active),
        "non_dominant_bbox": bbox,
        "non_dominant_component_sizes": components,
        "offset_min": min(indexes),
        "offset_max": max(indexes),
        "center_tile_reference": indexes[32 * GRID_SIDE + 32],
        "first_tile_reference": indexes[0],
        "last_tile_reference": indexes[-1],
        "lowest_tile_data_address": min(target_offsets),
        "highest_tile_data_address": max(target_offsets),
        "validated_tile_refs": len(target_offsets),
    }


def inspect_auxiliary_pages(data):
    """Verify 3 true 4x2KB signed-pair bases at ROM $130612.

    The 68000 source at file $CCDE reads this pointer table; the routine at
    $76A2 reads two signed bytes from four 0x800-spaced pages. These are
    geometry *input buffers*, not established course names or vertex meshes.
    """
    pages = []
    for i, expected in enumerate(AUXILIARY_PAGE_POINTERS):
        p = be32(data, AUXILIARY_PAGE_TABLE + 4 * i)
        if p != expected:
            raise ValueError(f"auxiliary data page {i} differs from disassembly")
        offset = translate_pointer(p, len(data))
        if offset + 3 * 0x800 + 2 > len(data):
            raise ValueError("auxiliary four-page read extends past ROM")
        pages.append({
            "pointer_rom_offset": AUXILIARY_PAGE_TABLE + 4 * i,
            "base_rom_offset": offset,
            "page_stride": 0x800,
            "page_count": 4,
            "signed_byte_pairs_valid": 4,
        })
    return pages


def inspect_image(data, require_identity=True):
    if require_identity:
        if len(data) != ROM_BYTES or hashlib.sha256(data).hexdigest() != ROM_SHA256:
            raise ValueError("32X ROM hash/size does not match the verified user dump")
        if hashlib.md5(data).hexdigest() != ROM_MD5:
            raise ValueError("32X reference disassembly MD5 disagrees with the donor")
        if not data[0x100:0x120].startswith(b"SEGA 32X U") or data[0x3C0:0x3CF] != b"MARS CHECK MODE":
            raise ValueError("32X header or MARS mode header mismatches")
        # 68K machine-code location, not a base for DATA address translation.
        if data[0x73E8:0x73EE] != bytes.fromhex("26 3c 00 00 04 00"):
            raise ValueError("68K grid-lookup code signature differs")
    normal = [inspect_slot(data, NORMAL_TABLE, i) for i in range(6)]
    if require_identity:
        for result, ptrs in zip(normal, NORMAL_PAIRS):
            if result is None:
                raise ValueError("missing normal 32X lookup")
            expected = tuple(translate_pointer(x, len(data)) for x in ptrs)
            if (result["map_rom_offset"], result["base_data_rom_offset"]) != expected:
                raise ValueError("normal lookup differs from audited 68000 table")
    alternative = [inspect_slot(data, ALTERNATE_TABLE, i) for i in range(6)]
    found_alt = [x for x in alternative if x is not None]
    if require_identity:
        if len(found_alt) != 1 or found_alt[0]["mode_slot"] != ALTERNATE_PAIR[0]:
            raise ValueError("alternate lookup slot differs from source disassembly")
        expected = tuple(translate_pointer(x, len(data)) for x in ALTERNATE_PAIR[1:])
        if (found_alt[0]["map_rom_offset"], found_alt[0]["base_data_rom_offset"]) != expected:
            raise ValueError("alternate lookup pointers differ from disassembly")
    return {
        "status": "rom_sparse_road_lookups_confirmed_not_3d_meshes",
        "sha256": hashlib.sha256(data).hexdigest(),
        "source_32x_disassembly": "https://github.com/matiaszanolli/sega-vr-disasm/blob/master/analysis/TRACK_DATA_FORMAT.md",
        "corrected_32x_data_window_base": DATA_FILE_BASE,
        "normal": normal,
        "alternate": found_alt,
        "auxiliary_4_page_geometry_inputs": inspect_auxiliary_pages(data) if require_identity else [],
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
