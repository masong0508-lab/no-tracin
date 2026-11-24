"""Synthetic bytes only: model strip semantics, original grid and gate tables."""
import struct
import tempfile
import unittest
from pathlib import Path
from tools import export_vrd32x_sh2_scene as s


def pack(rom, at, fmt, *values):
    struct.pack_into('>' + fmt, rom, at, *values)


def fixture():
    rom = bytearray(0x180000)
    off = 0x100000
    pointer = 0x22000000 + off
    pack(rom, off, 'HH12h', 4, 0, -64, 0, 64, 64, 0, 64, 64, 0, -64, -64, 0, -64)
    # Full quad; strip sharing v3->v0 and v2->v1; triangle with source normal.
    pack(rom, off+28, '16H', 0, 0x4600, 0, 16, 32, 48,
         0x0400, 0x4700, 16, 0, 0x0221, 0x4801, 32, 0, 0x4000, 0)
    pack(rom, off+60, 'H', 0x0C00)
    for grid in s.GRIDS:
        for cell in range(1024):
            pack(rom, grid+4*cell, 'I', s.EMPTY)
        for cell in (33, 34):
            pack(rom, grid+4*cell, 'I', pointer)
    base, frames = 0x10000, 0x11000
    for slot in range(5):
        pack(rom, s.PROP_TABLE+4*slot, 'I', s.M68K_BASE+base)
    for i in range(3):
        pack(rom, base+14*i, 'hhhhhI', 100+i, -200, 300, 0x1000, 0x100, s.M68K_BASE+frames)
    pack(rom, frames, '6I', *([pointer]*6))
    return rom, pointer


class SceneTest(unittest.TestCase):
    def test_polygon_strip_and_normals(self):
        rom,pointer = fixture()
        mesh = s.decode_model(rom, pointer)
        self.assertEqual(mesh['vertices'][0], (-64,0,64))
        self.assertEqual([f['vertices'] for f in mesh['faces']], [[0,1,2,3],[3,2,1,0],[3,0,2]])
        self.assertEqual([f['palette_index'] for f in mesh['faces']], [0x46,0x47,0x48])
        self.assertEqual(mesh['faces'][-1]['normal'], (0,0x4000,0))
        self.assertEqual(mesh['end_offset'], 0x100000+62)

    def test_grid_and_landmark_source_deduplication(self):
        rom,pointer = fixture()
        scene = s.extract_course(rom, 2)
        self.assertEqual(scene['course'], 'Acropolis')
        self.assertEqual([(c['row'],c['column']) for c in scene['cells']], [(1,1),(1,2)])
        self.assertEqual(len(scene['meshes']),1)
        self.assertEqual([r['role'] for r in scene['roadside']], ['start_gate','checkpoint_1','checkpoint_2'])
        self.assertEqual(scene['roadside'][0]['position'], (100,-200,300))
        self.assertEqual(scene['roadside'][0]['model_pointers'], (pointer,)*6)
        self.assertFalse(scene['palette_rgb_recovered'])
        with tempfile.TemporaryDirectory() as td:
            target=Path(td)/'track.obj';s.write_obj(scene,target)
            obj=target.read_text()
            self.assertEqual(sum(l.startswith('v ') for l in obj.splitlines()),4)
            self.assertIn('v -2.00000 0.00000 2.00000',obj)
            self.assertIn('f 4 3 2 1',obj)
            self.assertIn('usemtl palette_48',obj)

    def test_invalid_pointer_truncation_and_index(self):
        rom,pointer=fixture()
        for invalid in (0,0x06000000,pointer+1):
            with self.assertRaises(ValueError):s.decode_model(rom,invalid)
        with self.assertRaises(ValueError):s.decode_model(rom[:0x100000+50],pointer)
        pack(rom,0x100000+32,'H',0x400)
        with self.assertRaisesRegex(ValueError,'outside'):s.decode_model(rom,pointer)

    def test_uninitialized_strip_and_empty_grid(self):
        rom,pointer=fixture()
        pack(rom,0x100000+28,'H',0x0200)
        with self.assertRaisesRegex(ValueError,'uninitialized'):s.decode_model(rom,pointer)
        for cell in (33,34):pack(rom,s.GRIDS[0]+cell*4,'I',s.EMPTY)
        with self.assertRaisesRegex(ValueError,'no actual source'):s.grid_models(rom,0)


if __name__=='__main__':unittest.main()
