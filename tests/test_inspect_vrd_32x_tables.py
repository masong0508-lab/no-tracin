"""Actual 68000 pointer-table math, source-independent unit fixtures."""
import os
from pathlib import Path
import tempfile
import unittest
from tools.inspect_vrd_32x_tables import (
    ALTERNATE_TABLE, GRID_BYTES, NORMAL_TABLE, be16, be32,
    inspect_slot, inspect_image, translate_pointer,
)


def fixture():
    image = bytearray(0x20000)
    # Two valid pointers with a 64×64 grid of unsigned big-endian tile IDs.
    image[NORMAL_TABLE:NORMAL_TABLE + 4] = (0x880800).to_bytes(4, 'big')
    image[NORMAL_TABLE + 4:NORMAL_TABLE + 8] = (0x884000).to_bytes(4, 'big')
    for cell in range(4096):
        image[0x800 + cell * 2: 0x802 + cell * 2] = (cell % 91).to_bytes(2, 'big')
    return image


class Inspect32XTablesTests(unittest.TestCase):
    def test_unsigned_center_and_all_targets(self):
        raw = fixture()
        result = inspect_slot(raw, NORMAL_TABLE, 0)
        self.assertEqual(result['grid_cells'], 4096)
        self.assertEqual(result['validated_tile_refs'], 4096)
        self.assertEqual(result['distinct_tile_references'], 91)
        self.assertEqual(result['center_tile_reference'], 2080 % 91)
        self.assertEqual(result['map_rom_offset'], 0x800)
        self.assertEqual(result['base_data_rom_offset'], 0x4000)
        self.assertIsNone(inspect_slot(raw, ALTERNATE_TABLE, 0))

    def test_invalid_pointers_fail_closed(self):
        raw = fixture()
        raw[NORMAL_TABLE:NORMAL_TABLE + 4] = (0x00c000).to_bytes(4, 'big')
        with self.assertRaisesRegex(ValueError, 'unmapped'):
            inspect_slot(raw, NORMAL_TABLE, 0)
        raw = fixture()
        raw[NORMAL_TABLE + 4:NORMAL_TABLE + 8] = (0x89FFFF).to_bytes(4, 'big')
        with self.assertRaisesRegex(ValueError, 'out of ROM'):
            inspect_slot(raw, NORMAL_TABLE, 0)
        raw = fixture()
        raw[NORMAL_TABLE:NORMAL_TABLE + 4] = b'\x00' * 4
        with self.assertRaisesRegex(ValueError, 'partially filled'):
            inspect_slot(raw, NORMAL_TABLE, 0)

    def test_reject_wrong_revision_without_export(self):
        with self.assertRaisesRegex(ValueError, 'hash/size'):
            inspect_image(fixture())

    def test_verified_private_32x_rom(self):
        path = os.environ.get('VR32X_ROM')
        if not path:
            self.skipTest('Set VR32X_ROM to check exact user-supplied image')
        path = Path(path)
        before = path.stat().st_mtime_ns
        contents = path.read_bytes()
        result = inspect_image(contents)
        self.assertEqual(len(result['normal']), 6)
        self.assertEqual(len(result['alternate']), 1)
        self.assertEqual([x['distinct_tile_references'] for x in result['normal']],
                         [1460, 1237, 1549, 1670, 1580, 1526])
        self.assertEqual([x['center_tile_reference'] for x in result['normal']],
                         [0x404, 0xEBE5, 0x2F4, 0x2A0, 0xB78B, 0x45])
        self.assertEqual(result['alternate'][0]['distinct_tile_references'], 1569)
        self.assertEqual(path.read_bytes(), contents)
        self.assertEqual(path.stat().st_mtime_ns, before)


if __name__ == '__main__':
    unittest.main()
