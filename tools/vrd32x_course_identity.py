"""Prove original 32X course selection -> normal BSP table slots 0..4.

The actual 68000 code moves the course selection byte $A019 into $FEA5,
loads it as D0 for game_mode_track_config, saves D0*4 at $C8A0, and
uses $C8A0*2 as the index into the six-pair normal road BSP lookup.
The unused sixth lookup is NOT a sixth normal named course. A separate D1
selects the original three-car configuration and does not name the map.

Names follow the original U.S. Virtua Racing Deluxe manual's five-course
selection order. This verifies the link to original source *road geometry*,
not Sega's original SH-2 scenery meshes, race start or lap checkpoints.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib

try:
    from tools.inspect_vrd_32x_tables import (
        ROM_BYTES, ROM_SHA256, NORMAL_TABLE, NORMAL_PAIRS, be32,
        inspect_image,
    )
except ModuleNotFoundError:
    from inspect_vrd_32x_tables import (
        ROM_BYTES, ROM_SHA256, NORMAL_TABLE, NORMAL_PAIRS, be32,
        inspect_image,
    )

COURSE_NAMES = ('BIG FOREST', 'BAY BRIDGE', 'ACROPOLIS', 'HIGHLAND', 'SAND PARK')
COURSE_SHORT_NAMES = ('FOREST', 'BRIDGE', 'ACRO', 'HIGHLND', 'SAND')
# 68000 file offsets, disassembled from Sega's verified USA cartridge.
# All signatures are DATA ABOUT PUBLICLY DOCUMENTED MACHINE CODE, not game
# assets. Verify every link: menu choice, D0 course config, and BSP lookup.
SELECTION_WITNESSES = (
    (0x00E040, '11 f8 a0 19 fe a5', 'menu course $A019 -> $FEA5'),
    (0x004A70, '10 38 fe a5 12 38 fe b1', 'race loads course D0, car D1'),
    (0x004A84, '4e b9 00 88 d1 9c', 'init calls course/car index setter'),
    (0x00D1B2, '31 c0 c8 9c d0 40 31 c0 c8 9e d0 40 31 c0 c8 a0',
     'course D0*4 stored at $C8A0'),
    (0x007406, '30 38 c8 a0 d0 40 45 fa 00 1e',
     'road lookup reads course D0*8 at table $742C'),
    (0x00741A, '22 72 00 00 36 31 30 00 22 72 00 04',
     'lookup indexes original source grid and BSP bank'),
    (0x0053DC, '10 38 fe b6 0c 00 00 05 65 02 70 00',
     'original alternate race scene clamps course indices to 0..4'),
)


def verify_course_identity(rom: bytes, *, verify_full_rom: bool = True) -> dict:
    """Return the five real course names only when source code agrees.

    Does not guess the start/finish, AI route, checkpoint order or SH-2
    scenery. Never accepts an unsupported ROM image as authentic.
    """
    if verify_full_rom:
        inspect_image(rom, require_identity=True)
        if len(rom) != ROM_BYTES or hashlib.sha256(rom).hexdigest() != ROM_SHA256:
            raise ValueError('unverified original 32X source cartridge')
    if len(rom) < 0x140000:
        raise ValueError('incomplete 68000 original source image')
    for offset, hex_signature, why in SELECTION_WITNESSES:
        expected = bytes.fromhex(hex_signature)
        if rom[offset:offset+len(expected)] != expected:
            raise ValueError(f'cannot prove original 32X course identity: {why} at ${offset:06x}')
    slots = []
    for i, name in enumerate(COURSE_NAMES):
        pointers = (be32(rom, NORMAL_TABLE + 8*i),
                    be32(rom, NORMAL_TABLE + 8*i+4))
        if pointers != NORMAL_PAIRS[i]:
            raise ValueError(f'original course {i} BSP data pointers differ')
        slots.append(dict(course_index=i, name=name,
                          selector='original menu $A019 -> D0 -> $C8A0 -> source BSP',
                          source_normal_grid_pointer=pointers[0],
                          source_normal_bank_pointer=pointers[1],
                          source_68000_road_slot_verified=True,
                          original_sh2_scenery_verified=False,
                          original_racing_line_verified=False,
                          original_start_finish_verified=False))
    return {'source_index_to_original_course_order_verified': True,
            'selection_code_witnesses': len(SELECTION_WITNESSES),
            'name_order_basis': 'US original 32X Deluxe five-course manual order',
            'source_road_mode_count': len(slots),
            'original_racing_modes_completed': False,
            'slots': slots}
