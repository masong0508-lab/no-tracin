#!/usr/bin/env python3
"""Verify VR Deluxe 32X variable-length road-geometry record boundaries.

For verified slots 0..4 and alternate 3, each non-background grid index
points to a record beginning with an unsigned 16-bit count N. The next
record starts (15 + 14*N) 16-bit words later. The source ROM itself proves
all these strides; the 28-byte subrecords are NOT yet decoded as 3D vertices.

Slot 5 uses the slot-4 data bank but does not obey this record format. It is
reported as unresolved instead of fabricating geometry/courses from it.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
try:  # python -m tools.inspect_vrd_32x_records, or tests importing tools.*
    from tools.inspect_vrd_32x_tables import GRID_SIDE, NORMAL_TABLE, ALTERNATE_TABLE, be16, inspect_image, inspect_slot
except ModuleNotFoundError:  # python tools/inspect_vrd_32x_records.py from repo root
    from inspect_vrd_32x_tables import GRID_SIDE, NORMAL_TABLE, ALTERNATE_TABLE, be16, inspect_image, inspect_slot


def record_boundaries(data, table_offset, slot):
    info = inspect_slot(data, table_offset, slot)
    if info is None:
        raise ValueError("missing lookup pair")
    map_off = info["map_rom_offset"]
    base_off = info["base_data_rom_offset"]
    words = [be16(data, map_off + 2 * i) for i in range(GRID_SIDE**2)]
    dominant = info["dominant_index"]
    active = sorted(set(words) - {dominant})
    if not active or active[0] != 0:
        raise ValueError("record stream does not begin at offset zero")
    if dominant != max(words):
        raise ValueError("background marker not terminal in record stream")
    if len(active) != info["non_dominant_cells"]:
        raise ValueError("some active grid tiles alias record starts")
    offsets = active + [dominant]
    bad = []
    histogram = Counter()
    for current, following in zip(offsets[:-1], offsets[1:]):
        count = be16(data, base_off + current * 2)
        histogram[count] += 1
        next_offset = current + 15 + 14 * count
        if next_offset != following:
            bad.append({"record_word_offset": current, "actual_next": following,
                        "declared_next": next_offset, "header_word": count})
    marker = be16(data, base_off + 2 * dominant)
    result = {
        "table_kind": "alternate" if table_offset == ALTERNATE_TABLE else "normal",
        "slot": slot,
        "record_count": len(active),
        "record_header_formula": "next_word = this_word + 15 + 14 * header_count",
        "first_record_word_offset": active[0],
        "end_marker_word_offset": dominant,
        "end_marker_word": marker,
        "subrecords_total": sum((header + 1) * count for header, count in histogram.items()) if not bad else None,
        "subrecord_word_width": 14,
        "source_data_start_file_offset": base_off,
        "source_data_end_file_offset": base_off + dominant * 2 + 2,
        "record_header_histogram": {str(k): v for k, v in sorted(histogram.items()) if k <= 32},
        "boundary_errors": len(bad),
        "first_invalid_record": bad[0] if bad else None,
        "record_structure_confirmed": not bad and marker == 0xFFFF,
        "geometry_fields_interpreted": False,
    }
    return result


def inspect_records(data, require_identity=True):
    # A bad cartridge identity or 68000 table layout must be rejected first.
    tables = inspect_image(data, require_identity=require_identity)
    normal = [record_boundaries(data, NORMAL_TABLE, i) for i in range(6)]
    alternate = record_boundaries(data, ALTERNATE_TABLE, 3)
    if require_identity:
        if any(not x["record_structure_confirmed"] for x in normal[:5]):
            raise ValueError("a verified 32X main road record stream has invalid boundaries")
        if not alternate["record_structure_confirmed"]:
            raise ValueError("32X alternate road record stream has invalid boundaries")
        if normal[5]["record_structure_confirmed"]:
            raise ValueError("unresolved slot 5 unexpectedly shares verified record grammar")
    verified = normal[:5] + [alternate]
    return {
        "status": "verified_32x_variable_length_record_boundaries_not_vertex_geometry",
        "sha256": tables["sha256"],
        "normal": normal,
        "alternate": alternate,
        "fully_parsed_normal_slots": [r["slot"] for r in normal if r["record_structure_confirmed"]],
        "exceptional_normal_slot": 5,
        "verified_record_count": sum(r["record_count"] for r in verified),
        "verified_subrecord_count": sum(r["subrecords_total"] for r in verified),
        "height_and_vertex_semantics_recovered": False,
        "all_five_deluxe_courses_imported": False,
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("rom", type=Path, help="Unmodified US Virtua Racing Deluxe 32X image")
    p.add_argument("--output", type=Path, help="Optional JSON statistics output")
    args = p.parse_args()
    content = json.dumps(inspect_records(args.rom.read_bytes()), indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(content)
    else:
        print(content, end="")


if __name__ == "__main__":
    main()
