#!/usr/bin/env python3
"""Read-only Sega Virtua Racing Deluxe 32X road-region and height-plane sampler.

This follows the verified US 32X 68000 routines in
matiaszanolli/sega-vr-disasm:
  $0073E8  track_data_index_calc_table_lookup (64x64 tile grid)
  $00748C  angle_normalize_bsp (N+1 groups, each 28 bytes)
  $0075C8  plane_eval_pair (2D surface-height plane)

For each tile the word at the lookup address selects a *counted stream* of
28-byte groups. The first word of each group encodes four successive plane
half-space tests. The group contains two words per test; words 9, 10 and 11
are the height-plane coefficients/offset, and byte 25 selects the arithmetic
shift. These are real collision/height data, NOT mesh vertices, centerlines,
course names, scenery, or gameplay-ready race paths.

Five normal streams (slots 0..4) and alternate slot 3 have verified record
boundaries. Normal slot 5 has unrelated lookup semantics and is rejected.

No source ROM bytes are copied, exported, changed or added to the project.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import NamedTuple

try:
    from tools.inspect_vrd_32x_tables import (
        ROM_SHA256, ROM_BYTES, NORMAL_TABLE, ALTERNATE_TABLE,
        GRID_SIDE, be16, inspect_image, inspect_slot, coordinate_grid_index,
    )
except ModuleNotFoundError:
    from inspect_vrd_32x_tables import (
        ROM_SHA256, ROM_BYTES, NORMAL_TABLE, ALTERNATE_TABLE,
        GRID_SIDE, be16, inspect_image, inspect_slot, coordinate_grid_index,
    )


class SurfaceHit(NamedTuple):
    """Results from the original 68000's road region and height decisions."""
    group: int
    height: int  # 68000 plane_eval raw output; world scale unverified
    coeff_x: int
    coeff_z: int
    coeff_c: int
    attrib: int  # original 16-bit metadata; do not guess meaning
    tail_word: int  # original 16-bit metadata; do not guess meaning


def signed16(v: int) -> int:
    return v if v < 0x8000 else v - 0x10000


def signed32(v: int) -> int:
    return (v + (1 << 31)) % (1 << 32) - (1 << 31)


def asl16_with_carry(value: int) -> tuple[int, bool]:
    return (value << 1) & 0xFFFF, bool(value & 0x8000)


def plane_eval(data: bytes, group_start: int, x_local: int, z_local: int) -> SurfaceHit:
    """The original signed MULS.W/ASR.L plane at $75C8/$75E0.

    x_local/z_local are normalized *word* coordinates (0..1022) from
    (world+0x4000)&0x1FF, then multiplied by two.
    """
    if group_start < 0 or group_start + 28 > len(data):
        raise ValueError("plane group outside ROM")
    ax = signed16(be16(data, group_start + 18))
    az = signed16(be16(data, group_start + 20))
    constant = signed16(be16(data, group_start + 22))
    attrib = be16(data, group_start + 24)
    tail = be16(data, group_start + 26)
    if not (0 <= x_local <= 1022 and 0 <= z_local <= 1022):
        raise ValueError("unnormalized plane input")
    # MULS.W produces a 32-bit signed product. The 68000 ADD.L wraps to
    # 32 bits; ASR.L then sign-extends, including on negative ground slopes.
    h = signed32(ax * x_local + az * z_local + 32 * constant)
    h >>= 5 if (attrib & 0x80) else 6  # TST.B $19(A2) followed by BMI
    return SurfaceHit(-1, h, ax, az, constant, attrib, tail)


def test_bsp_group(data: bytes, group_start: int, x_word: int, z_word: int) -> bool:
    """Return True when the original group identifies this point as inside.

    Mirrors the branch decisions and 4x MULS/direct tests in the original
    68000 `angle_normalize` $748C loop. A successful half-space skip exits
    this group and advances by exactly 28 bytes. If *none* of four tests
    skips the group, the 68000 returns D0=1 and leaves A2 at this group.
    """
    if group_start < 0 or group_start + 28 > len(data):
        raise ValueError("BSP group outside ROM")
    if not (0 <= x_word <= 1022 and 0 <= z_word <= 1022):
        raise ValueError("BSP point not in 68000 local-coordinate domain")
    selector = be16(data, group_start)
    cursor = group_start + 2

    for _ in range(4):
        selector, b0 = asl16_with_carry(selector)
        selector, b1 = asl16_with_carry(selector)
        if b1:
            # Direct compare; the code skips two word slots regardless of
            # how many it actually reads. A0/A1 are unchanged on success.
            threshold = signed16(be16(data, cursor))
            cursor += 4
            lhs = z_word if b0 else x_word
            is_less = lhs < threshold
            selector, accept_bit = asl16_with_carry(selector)
            # D1 direct: negative -> BCS found; positive -> BCC found.
            # D2 direct: negative -> BCC found; positive -> BCS found.
            skip = (is_less == accept_bit) if not b0 else (is_less != accept_bit)
        else:
            # Coefficient/constant halfspace: signed MULS.W + ASR.L #9.
            coeff = signed16(be16(data, cursor))
            const = signed16(be16(data, cursor + 2))
            cursor += 4
            if b0:
                bound = signed32(z_word * coeff + const * 512) >> 9
                is_less = x_word < bound
                selector, accept_bit = asl16_with_carry(selector)
                # D2 coef: negative -> BCS found; positive -> BCC found.
                skip = (is_less == accept_bit)
            else:
                bound = signed32(x_word * coeff + const * 512) >> 9
                is_less = z_word < bound
                selector, accept_bit = asl16_with_carry(selector)
                # D1 coef: negative -> BCC found; positive -> BCS found.
                skip = (is_less != accept_bit)
        if skip:
            return False  # try next 28-byte group

    if cursor > group_start + 18:
        raise ValueError("BSP plane selectors consumed height coefficients")
    return True


class RoadSurface:
    """One ROM-backed 32X lookup slot, no data export or ROM mutation."""
    def __init__(self, data: bytes, slot: int, alternate: bool = False, *, verify_identity: bool = True):
        if not 0 <= slot < 6:
            raise ValueError("invalid 32X lookup slot")
        if slot == 5 and not alternate:
            raise ValueError("normal slot 5 has unverified record grammar")
        if alternate and slot != 3:
            raise ValueError("only alternate slot 3 has verified record grammar")
        if verify_identity:
            inspect_image(data, require_identity=True)
        kind = ALTERNATE_TABLE if alternate else NORMAL_TABLE
        info = inspect_slot(data, kind, slot)
        if info is None:
            raise ValueError("empty ROM road grid")
        self.rom = data
        self.slot = slot
        self.alternate = alternate
        self.grid = tuple(be16(data, info['map_rom_offset'] + 2 * i) for i in range(GRID_SIDE**2))
        self.background = info['dominant_index']
        self.bank_start = info['base_data_rom_offset']
        self.limit_word = self.background
        self.active_cells = info['non_dominant_cells']

    def sample_local(self, row: int, col: int, x: int, z: int) -> SurfaceHit | None:
        """x,z = within a single 512x512 world grid cell, range 0..511."""
        if not (0 <= row < GRID_SIDE and 0 <= col < GRID_SIDE
                and 0 <= x < 512 and 0 <= z < 512):
            raise ValueError("sample lies outside its 32X road-grid cell")
        offset = self.grid[row * GRID_SIDE + col]
        if offset == self.background:
            return None
        if not (0 <= offset < self.background):
            raise ValueError("invalid record offset for this cell")
        start = self.bank_start + 2 * offset
        count = be16(self.rom, start)
        end = offset + 15 + 14 * count
        if count > 64 or end > self.limit_word:
            raise ValueError("bad counted BSP record offset/length")
        wx, wz = x * 2, z * 2
        for group in range(count + 1):
            pos = start + 2 + 28 * group
            if test_bsp_group(self.rom, pos, wx, wz):
                result = plane_eval(self.rom, pos, wx, wz)
                return result._replace(group=group)
        return None

    def sample_world(self, world_x: int, world_z: int) -> SurfaceHit | None:
        i = coordinate_grid_index(world_x, world_z)
        row, col = divmod(i, GRID_SIDE)
        # The 68000 uses 16-bit ADDI #$4000 then ANDI #$01FF; only the
        # lowest nine world-coordinate bits survive this normalization.
        return self.sample_local(row, col,
                                 (world_x + 0x4000) & 0x1FF,
                                 (world_z + 0x4000) & 0x1FF)


def adjacent_edge_profile(road: RoadSurface, subcell_samples=(64,128,256,384,448)) -> dict:
    """Independent seam check: height planes must agree at shared edges.

    Adjacent samples are one *world unit* apart (511 vs 0). This is a
    statistical validation and does not assume route/course identity.
    """
    deltas = []
    mismatches = 0
    for i, offset in enumerate(road.grid):
        if offset == road.background:
            continue
        row, col = divmod(i, GRID_SIDE)
        neighbors = []
        if col < GRID_SIDE - 1 and road.grid[i + 1] != road.background:
            neighbors.append((row,col+1,0,1))
        if row < GRID_SIDE - 1 and road.grid[i + GRID_SIDE] != road.background:
            neighbors.append((row+1,col,1,0))
        for r2,c2,axis,tangent in neighbors:
            for along in subcell_samples:
                if axis == 0: # next tile to the right
                    left = road.sample_local(row,col,511,along)
                    right = road.sample_local(r2,c2,0,along)
                else:         # next tile below
                    left = road.sample_local(row,col,along,511)
                    right = road.sample_local(r2,c2,along,0)
                if (left is None) != (right is None):
                    mismatches += 1
                elif left is not None and right is not None:
                    deltas.append(abs(left.height - right.height))
    deltas.sort()
    return {
        'slot': road.slot,
        'alternate': road.alternate,
        'shared_inside_edges': len(deltas),
        'road_classification_disagreements': mismatches,
        'height_difference_p50': deltas[len(deltas)//2] if deltas else None,
        'height_difference_p90': deltas[int(len(deltas)*.90)] if deltas else None,
        'height_difference_p99': deltas[int(len(deltas)*.99)] if deltas else None,
        'height_difference_max': max(deltas) if deltas else None,
    }


def summarize_surface(data: bytes) -> dict:
    inspect_image(data, require_identity=True)
    profiles = []
    for slot in range(5):
        r = RoadSurface(data, slot, verify_identity=False)
        stats = adjacent_edge_profile(r)
        profiles.append(stats)
    alt = RoadSurface(data, 3, True, verify_identity=False)
    return {
        'status': '32x_bsp_boundary_and_height_planes_recovered_not_playable_geometry',
        'sha256': hashlib.sha256(data).hexdigest(),
        'source_68000_functions': ['$00748C angle_normalize_bsp', '$0075C8 plane_eval_pair'],
        'profiles': profiles,
        'alt_slot_3_non_background_cells': alt.active_cells,
        'special_slot_5': 'different record format; deliberately unsupported',
        'imported_playable_courses': 0,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('rom', type=Path, help='User-owned unmodified Virtua Racing Deluxe 32X ROM')
    ap.add_argument('--output',type=Path,help='Optional JSON statistics (no ROM or heightmap bytes)')
    args=ap.parse_args()
    report=json.dumps(summarize_surface(args.rom.read_bytes()), indent=2,sort_keys=True)+'\n'
    if args.output:
        args.output.write_text(report)
    else:
        print(report,end='')


if __name__=='__main__':
    main()
