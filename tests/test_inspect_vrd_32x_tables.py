"""Verify 32X physical address mapping and the actual sparse road map grids."""
import os
from pathlib import Path
import unittest
from tools.inspect_vrd_32x_tables import (
    ALTERNATE_TABLE, DATA_FILE_BASE, GRID_BYTES, NORMAL_TABLE,
    ROM_SHA256, be16, be32, coordinate_grid_index, inspect_slot,
    inspect_image, translate_pointer,
)


def fixture():
    # 0x90xxxx *data* pointers map to physical 0x10xxxx, not 0x08xxxx.
    image = bytearray(0x110000)
    image[NORMAL_TABLE:NORMAL_TABLE + 4] = (0x900800).to_bytes(4, 'big')
    image[NORMAL_TABLE + 4:NORMAL_TABLE + 8] = (0x904000).to_bytes(4, 'big')
    for cell in range(4096):
        image[0x100800 + cell * 2: 0x100802 + cell * 2] = (cell % 91).to_bytes(2, 'big')
    return image


class Inspect32XTablesTests(unittest.TestCase):
    def test_correct_data_bank_not_old_wrong_bank(self):
        self.assertEqual(translate_pointer(0x94C000, 0x300000), 0x14C000)
        self.assertEqual(translate_pointer(0x970000, 0x300000), 0x170000)
        self.assertEqual(translate_pointer(0x93061E, 0x300000), 0x13061E)
        self.assertNotEqual(translate_pointer(0x94C000, 0x300000), 0xCC000)
        with self.assertRaisesRegex(ValueError, 'unmapped'):
            translate_pointer(0x880800, 0x300000)

    def test_unsigned_center_and_all_targets(self):
        raw = fixture()
        result = inspect_slot(raw, NORMAL_TABLE, 0)
        self.assertEqual(result['grid_cells'], 4096)
        self.assertEqual(result['validated_tile_refs'], 4096)
        self.assertEqual(result['distinct_tile_references'], 91)
        self.assertEqual(result['center_tile_reference'], 2080 % 91)
        self.assertEqual(result['map_rom_offset'], 0x100800)
        self.assertEqual(result['base_data_rom_offset'], 0x104000)
        self.assertIsNone(inspect_slot(raw, ALTERNATE_TABLE, 0))

    def test_68000_world_to_cell_address_math(self):
        self.assertEqual(coordinate_grid_index(0, 0), 2080)
        self.assertEqual(coordinate_grid_index(-16384, -16384), 0)
        self.assertEqual(coordinate_grid_index(16383, 16383), 4095)
        self.assertEqual(coordinate_grid_index(-1, 0), 2079)
        self.assertEqual(coordinate_grid_index(0, -1), 2016)
        for x, z in ((16384, 0), (0, 16384), (-16385, 0), (0, -16385)):
            with self.subTest(x=x, z=z):
                with self.assertRaisesRegex(ValueError, 'outside'):
                    coordinate_grid_index(x, z)

    def test_invalid_pointers_fail_closed(self):
        raw = fixture()
        raw[NORMAL_TABLE:NORMAL_TABLE + 4] = (0x00c000).to_bytes(4, 'big')
        with self.assertRaisesRegex(ValueError, 'unmapped'):
            inspect_slot(raw, NORMAL_TABLE, 0)
        raw = fixture()
        raw[NORMAL_TABLE + 4:NORMAL_TABLE + 8] = (0x90ffff).to_bytes(4, 'big')
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
        self.assertEqual(result['sha256'], ROM_SHA256)
        self.assertEqual(result['corrected_32x_data_window_base'], DATA_FILE_BASE)
        self.assertEqual([x['distinct_tile_references'] for x in result['normal']],
                         [295, 335, 317, 254, 559, 119])
        self.assertEqual([x['non_dominant_cells'] for x in result['normal']],
                         [294, 334, 316, 253, 558, 118])
        self.assertEqual([x['non_dominant_component_sizes'] for x in result['normal']],
                         [[294], [334], [316], [253], [558], [118]])
        self.assertEqual([x['dominant_index'] for x in result['normal']],
                         [0x7DBA, 0x94A2, 0xB3E6, 0x6087, 0xA5A4, 0x35F2])
        self.assertEqual([x['map_rom_offset'] for x in result['normal']],
                         [0x14C000, 0x14E000, 0x150000, 0x154000, 0x158000, 0x152000])
        self.assertEqual(result['alternate'][0]['distinct_tile_references'], 16)
        self.assertEqual(result['alternate'][0]['non_dominant_cells'], 15)
        self.assertEqual([x['base_rom_offset'] for x in result['auxiliary_4_page_geometry_inputs']],
                         [0x13061E, 0x13261E, 0x13461E])
        self.assertEqual(path.read_bytes(), contents)
        self.assertEqual(path.stat().st_mtime_ns, before)


if __name__ == '__main__':
    unittest.main()
