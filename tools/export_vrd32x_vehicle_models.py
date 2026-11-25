#!/usr/bin/env python3
"""Recover original 32X car models from ROM, including banked SH-2 addresses.

Private output only. The US source cartridge and derived model arrays must not
be committed. Reference: original ROM SH-2 copy at $24664-$24692 and table
$246A8; 68000 model tables $156C8, $157A0, $158B4, $158E4.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import struct
from pathlib import Path

SHA256 = 'cc2917895842ba42ce871a07a327c5467a5b5df8b7999ccc4b7b5bea9d4caf6b'
BANK_TABLE = 0x246B0
BANK_DEST = 0x0603C000
PLAYER_TABLE = 0x156C8
DEMO_TABLE = 0x157A0
AXLE_TABLE = 0x158B4
OPPONENT_TABLE = 0x158E4
TYPES = ('formula', 'stock', 'prototype')
LODS = ('near', 'middle', 'far')


def read_at(data, offset, fmt):
    size = struct.calcsize('>' + fmt)
    if not 0 <= offset <= len(data) - size:
        raise ValueError(f'out-of-bounds read at {offset:#x}')
    return struct.unpack_from('>' + fmt, data, offset)


def code_row(rom, table, kind, count):
    pointer, = read_at(rom, table + 4 * kind, 'I')
    if pointer & 1 or not 0x880000 <= pointer < 0x900000:
        raise ValueError('invalid 68000 program table pointer')
    return read_at(rom, pointer - 0x880000, 'I' * count)


def bank_range(rom, index):
    first, last = read_at(rom, BANK_TABLE + 8 * index, 'II')
    if first & 1 or last & 1 or not 0x22000000 <= first < last <= 0x22300000:
        raise ValueError('invalid SH-2 cartridge bank range')
    lo, hi = first - 0x22000000, last - 0x22000000
    if hi > len(rom) or hi - lo > 0x4000:
        raise ValueError('bank exceeds cartridge or SH-2 destination region')
    return lo, hi


def decode_model(rom, bank, pointer):
    lo, hi = bank
    start = lo + pointer - BANK_DEST
    if pointer & 1 or not lo <= start < hi:
        raise ValueError(f'model pointer outside active bank: {pointer:#x}')
    # Restrict every read to the actual bank, not the remainder of the ROM.
    data = memoryview(rom)[lo:hi]
    cursor = start - lo
    count, second = read_at(data, cursor, 'HH')
    cursor += 4
    if not 1 <= count <= 1024:
        raise ValueError('invalid vertex count')
    vertices = [read_at(data, cursor + 6*i, 'hhh') for i in range(count)]
    cursor += 6 * count
    slots = [None] * 4
    normal = None
    faces = []

    def word():
        nonlocal cursor
        value, = read_at(data, cursor, 'H')
        cursor += 2
        return value

    def index():
        value = (word() // 16) % 1024
        if value >= count:
            raise ValueError('out-of-range vertex index')
        return value

    for _ in range(16384):
        header = word()
        kind = (header >> 8) & 14
        if kind == 12:
            if not faces:
                raise ValueError('empty model')
            return dict(pointer=pointer, rom_offset=start, end_offset=lo+cursor,
                        second_word=second, vertices=vertices, faces=faces)
        if kind not in (0, 2, 4, 6):
            raise ValueError('unknown car polygon strip type')
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
        if any(i is None for i in slots[:n]):
            raise ValueError('uninitialized strip vertex')
        if header & 0x20:
            normal = read_at(data, cursor, 'hhh')
            cursor += 6
        faces.append(dict(vertices=slots[:n], header=header, colour=colour,
                          palette_index=colour >> 8, normal=normal,
                          normal_updated=bool(header & 0x20)))
    raise ValueError('missing polygon terminator')


def batches(mesh, limit=48):
    """Preserve face order and coordinates, with local indices for bounded renderers."""
    if not 4 <= limit <= 255:
        raise ValueError('invalid batch vertex limit')
    result, vertices, faces, index = [], [], [], {}
    def flush():
        if faces:
            result.append(dict(vertices=vertices[:], faces=faces[:]))
            vertices.clear(); faces.clear(); index.clear()
    for face in mesh['faces']:
        needed = set(face['vertices']) - index.keys()
        if faces and (len(index)+len(needed)>limit or len(faces)==255):
            flush()
        local = []
        for old in face['vertices']:
            if old not in index:
                index[old] = len(vertices)
                vertices.append(mesh['vertices'][old])
            local.append(index[old])
        faces.append(dict(face, vertices=local))
    flush()
    return result


def extract(rom):
    cars = []
    for kind, name in enumerate(TYPES):
        active = bank_range(rom, kind)
        demo = bank_range(rom, 3)
        player = code_row(rom, PLAYER_TABLE, kind, 17)
        demo_row = code_row(rom, DEMO_TABLE, kind, 22)
        axles = code_row(rom, AXLE_TABLE, kind, 3)
        opponent = code_row(rom, OPPONENT_TABLE, kind, 16)
        if any(opponent[i] != 0 for i in range(0,16,4)) or opponent[1] != axles[0]:
            raise ValueError('inconsistent opponent model table')
        meshes = {}
        def add(bank_id, bank, ptr):
            key = f'{bank_id}:{ptr:08x}'
            if key not in meshes:
                model = decode_model(rom, bank, ptr)
                model['batches48'] = batches(model)
                meshes[key] = model
            return key
        def slots(row, bank_id, bank):
            return {str(i):add(bank_id,bank,p) for i,p in enumerate(row)
                    if BANK_DEST <= p < 0x06040000}
        player_slots = slots(player,kind,active)
        demo_slots = slots(demo_row,3,demo)
        if not all(str(i) in player_slots for i in range(6)):
            raise ValueError('missing body or wheel component')
        # $9xxxxx data pointers use the $800000 cartridge window, not the
        # $880000 program/table window. Scene local position is shift 8;
        # model vertices use shift 6 (neutral translation is raw_anchor * 4).
        anchor = player[9]
        if not 0x900000 <= anchor < 0xA00000 or anchor & 1:
            raise ValueError('invalid local attachment data pointer')
        anchors = [read_at(rom,anchor-0x800000+6*i,'hhh') for i in range(3)]
        cars.append(dict(name=name, type_index=kind, source_bank=active,
                         player_table_words=player, player_slots=player_slots,
                         display_table_words=demo_row, display_slots=demo_slots,
                         opponent_axles=[add(kind,active,p) for p in axles[1:]],
                         opponent_lods=[{lod:add(kind,active,opponent[c*4+j+1])
                                         for j,lod in enumerate(LODS)} for c in range(4)],
                         player_attachment_source=anchor,
                         player_attachment_xyz=anchors, meshes=meshes))
    return dict(source_sha256=SHA256, model_vertex_shift=6,
                attachment_position_shift=8, palette_rgb_recovered=False,
                runtime_rendering_validated=False, cars=cars)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--out', type=Path, default=Path('build/vrd32x-vehicles/models.json'))
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    if len(rom)!=3145728 or hashlib.sha256(rom).hexdigest()!=SHA256:
        parser.error('expected exact owner-supplied US Virtua Racing Deluxe ROM')
    pack = extract(rom)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(pack,separators=(',',':'))+'\n')
    for car in pack['cars']:
        mesh = car['meshes'][car['player_slots']['0']]
        print(car['name'],len(mesh['vertices']),'body vertices;',len(mesh['faces']),'faces;',
              len(car['meshes']),'bank-qualified source meshes')


if __name__ == '__main__':
    main()
