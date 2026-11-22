"""Regression witnesses for 68K candidate-only ROM scanning."""
import os
from pathlib import Path
import unittest
from tools.trace_racedrivin_genesis import scan, inspect

class TraceRaceDrivinTests(unittest.TestCase):
    def test_synthetic_jsr_and_signed_operators(self):
        b = bytearray(0x1200)
        b[4:8] = (0x200).to_bytes(4, 'big')
        b[0x200:0x206] = bytes.fromhex('4e b9 00 00 04 00')
        b[0x206:0x20c] = bytes.fromhex('4e f9 00 00 05 00')
        b[0x20c:0x210] = bytes.fromhex('c1 c0 81 c0')
        r = scan(b)
        self.assertEqual(r['jsr_absolute_long_total'], 1)
        self.assertEqual(r['top_referenced_rom_targets'][0]['offset'], 0x400)
        region = next(x for x in r['regions'] if x['offset_start'] == 0)
        self.assertEqual(region['jmp_absolute_long_refs'], 1)
        self.assertEqual(region['muls_word_patterns'], 1)
        self.assertEqual(region['divs_word_patterns'], 1)
        self.assertFalse(r['genesis_physics_routine_verified'])

    def test_unknown_cartridge_rejected(self):
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            inspect(bytes(0x800))

    def test_exact_supplied_rom(self):
        path = os.environ.get('RACE_DRIVIN_ROM')
        if not path: self.skipTest('Set RACE_DRIVIN_ROM to inspect exact private cartridge')
        path = Path(path)
        old_timestamp = path.stat().st_mtime_ns
        data = path.read_bytes()
        r = inspect(data)
        self.assertEqual(r['reset_pc'], 0x200)
        self.assertGreater(r['jsr_absolute_long_total'], 500)
        self.assertGreater(r['distinct_jsr_targets'], 90)
        self.assertFalse(r['genesis_physics_routine_verified'])
        self.assertEqual(path.read_bytes(), data)
        self.assertEqual(path.stat().st_mtime_ns, old_timestamp)

if __name__ == '__main__':
    unittest.main()
