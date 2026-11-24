#!/usr/bin/env python3
"""Private extraction of Deluxe's actual SH-2 track models, not BSP proxies.

Format reference (independent implementation): PokaMilkTea/
VirtuaRacingDeluxeEnhanced, src/hle.c, commit 99b5ff98473d958b4e6d3d987bce0ec16c57a869.
Requires the owner's exact US ROM. No donor bytes belong in source control.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import struct
from pathlib import Path

ROM_SHA256 = 'cc2917895842ba42ce871a07a327c5467a5b5df8b7999ccc4b7b5bea9d4caf6b'
COURSES = ('Big Forest', 'Bay Bridge', 'Acropolis', 'Highland', 'Sand Park')
GRIDS = (0x15C000, 0x15D000, 0x15E000, 0x15F000, 0x161000)
EMPTY = 0x2207FFFE
M68K_BASE = 0x880000
PROP_TABLE = 0x15A64


def unpack(rom, offset, fmt):
    size = struct.calcsize('>' + fmt)
    if offset < 0 or offset + size > len(rom):
        raise ValueError(f'truncated source read at {offset:#x}')
    return struct.unpack_from('>' + fmt, rom, offset)


def model_offset(rom, pointer):
    address = pointer & ~0x20000000
    offset = address - 0x02000000
    if offset < 0 or offset >= len(rom) or offset & 1:
        raise ValueError(f'invalid SH-2 cartridge model pointer {pointer:#010x}')
    return offset


def decode_model(rom, pointer):
    """Read untransformed signed vertices, strip faces, material and normal words."""
    start = model_offset(rom, pointer)
    count, second = unpack(rom, start, 'HH')
    if not 1 <= count <= 1024:
        raise ValueError(f'invalid model vertex count {count} at {start:#x}')
    vertices = [unpack(rom, start + 4 + i * 6, 'hhh') for i in range(count)]
    cursor = start + 4 + 6 * count
    slots = [None] * 4
    faces = []

    def word():
        nonlocal cursor
        value, = unpack(rom, cursor, 'H')
        cursor += 2
        return value

    def index():
        raw = word()
        value = (raw // 16) % 1024
        if value >= count:
            raise ValueError(f'model vertex index {raw:#x} outside {count}')
        return value

    # Hard bound catches a missing terminator, even when adjacent bytes look valid.
    for _ in range(16384):
        header = word()
        kind = (header >> 8) & 14
        if kind == 12:
            return {'rom_offset': start, 'second_word': second, 'vertices': vertices,
                    'faces': faces, 'end_offset': cursor}
        colour = word()
        if kind == 0:
            slots[0], slots[1] = index(), index()
        elif kind == 2:
            slots[1] = slots[3]
        elif kind == 4:
            slots[0], slots[1] = slots[3], slots[2]
        else:
            slots[0] = slots[2]
        slots[2] = index()
        slots[3] = slots[2] if colour & 1 else index()
        n = 3 if header & 1 else 4
        if any(v is None for v in slots[:n]):
            raise ValueError('model strip refers to an uninitialized vertex')
        normal = None
        if header & 0x20:
            normal = unpack(rom, cursor, 'hhh')
            cursor += 6
        faces.append({'vertices': slots[:n], 'header': header, 'colour': colour,
                      'palette_index': colour >> 8, 'normal': normal})
    raise ValueError(f'model at {start:#x} has no bounded polygon terminator')


def grid_models(rom, slot):
    if slot not in range(len(GRIDS)):
        raise ValueError('invalid course index')
    cells = []
    for cell in range(1024):
        pointer, = unpack(rom, GRIDS[slot] + cell * 4, 'I')
        if pointer == EMPTY:
            continue
        cells.append({'row': cell // 32, 'column': cell % 32,
                      'model_offset': model_offset(rom, pointer), 'pointer': pointer})
    if not cells:
        raise ValueError('no actual source track models in course grid')
    return cells


def m68k_offset(rom, address):
    offset = address - M68K_BASE
    if offset < 0 or offset >= len(rom) or offset & 1:
        raise ValueError(f'invalid 68000 cartridge pointer {address:#x}')
    return offset


def roadside_records(rom, slot):
    """Return original start/checkpoint gate records and documented special props.

    Raw angles and scale are preserved. No claim about official lap logic or
    dynamic model transforms: model geometry and instance records stay separate.
    """
    if slot not in range(len(COURSES)):
        raise ValueError('invalid course index')
    records = []
    def add(offset, role, *, frames=False, pointer=None, axis='y', paired=False):
        x, y, z, angle, scale = unpack(rom, offset, 'hhhhh')
        if pointer is None:
            pointer, = unpack(rom, offset + 10, 'I')
        models = (unpack(rom, m68k_offset(rom, pointer), '6I') if frames else (pointer,))
        # Some animation slots could be absent; retain their exact zero values.
        for p in models:
            if p:
                model_offset(rom, p)
        if not any(models):
            raise ValueError(f'empty roadside model list at {offset:#x}')
        records.append(dict(role=role, rom_offset=offset, position=(x,y,z),
                            angle=angle, angle_axis=axis, scale=scale,
                            paired=paired, model_pointers=models))
    table_pointer, = unpack(rom, PROP_TABLE + slot * 4, 'I')
    base = m68k_offset(rom, table_pointer)
    for i in range(3):
        add(base + 14*i, 'start_gate' if i == 0 else f'checkpoint_{i}', frames=True)
    if slot == 0:
        for offset, role in ((0x3924, 'ferris_wheel'), (0x3940, 'pirate_ship')):
            add(offset, role, paired=True)
            add(offset + 14, role + '_moving_part', paired=True)
    elif slot == 1:
        add(0x3A76, 'bay_static_prop', pointer=0x222A218E)
        add(0x3A80, 'bay_turning_prop', paired=True)
        add(0x3A8E, 'bay_turning_prop_part', paired=True)
        for i in range(4):
            add(0x3A4E + i*10, f'bay_shared_prop_{i}', pointer=0x22295A24)
    elif slot == 3:
        add(0x3A9C, 'highland_turning_prop', axis='z')
    return records


def extract_course(rom, slot):
    cells = grid_models(rom, slot)
    props = roadside_records(rom, slot)
    meshes = {}
    for p in [c['pointer'] for c in cells] + [p for r in props for p in r['model_pointers'] if p]:
        offset = model_offset(rom, p)
        if offset not in meshes:
            meshes[offset] = decode_model(rom, p)
    # Static track vertices are world-space, so a repeated pointer is not a
    # separate mesh instance. The cell associations remain in the JSON.
    return dict(course=COURSES[slot], source_grid_offset=GRIDS[slot],
                cells=cells, roadside=props, meshes=meshes,
                track_vertex_fixed_shift=11, prop_vertex_fixed_shift=6,
                coordinate_convention='raw signed SH-2 xyz; track model coordinates /32 in SH-2 world units',
                source_palette_indices=True, palette_rgb_recovered=False,
                animated_prop_transforms_applied=False, original_race_rules_verified=False)


def write_obj(scene, target):
    """Actual static meshes, including structures/terrain, with original material IDs.

    OBJ does not invent RGB values or transform local animated roadside models.
    """
    with target.open('w', encoding='utf-8') as out:
        out.write('# Original static 32X track mesh; SH-2 world units (raw / 32)\n')
        start = 1
        for offset in dict.fromkeys(c['model_offset'] for c in scene['cells']):
            mesh = scene['meshes'][offset]
            out.write(f'o source_{offset:06x}\n')
            for x,y,z in mesh['vertices']:
                out.write(f'v {x/32:.5f} {y/32:.5f} {z/32:.5f}\n')
            for face in mesh['faces']:
                out.write(f"usemtl palette_{face['palette_index']:02x}\n")
                out.write('f ' + ' '.join(str(start+i) for i in face['vertices']) + '\n')
            start += len(mesh['vertices'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--out-dir', type=Path, default=Path('build/vrd32x-sh2-scene'))
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    if len(rom) != 3145728 or hashlib.sha256(rom).hexdigest() != ROM_SHA256:
        parser.error('expected unmodified, owner-supplied US Virtua Racing Deluxe 32X cartridge')
    args.out_dir.mkdir(parents=True, exist_ok=True)
    for slot, name in enumerate(COURSES):
        scene = extract_course(rom, slot)
        stem = args.out_dir / name.lower().replace(' ', '-')
        stem.with_suffix('.json').write_text(json.dumps(scene, separators=(',', ':')))
        write_obj(scene, stem.with_suffix('.obj'))
        print(name, len(scene['cells']), 'source cells;', len(scene['meshes']), 'unique models')


if __name__ == '__main__':
    main()
