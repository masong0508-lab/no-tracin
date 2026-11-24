"""Exact U.S. 32X 68000 course-index mapping; synthetic and owned ROM tests."""
from pathlib import Path
import os
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from vrd32x_course_identity import (COURSE_NAMES, COURSE_SHORT_NAMES,
                                    SELECTION_WITNESSES, verify_course_identity)
from inspect_vrd_32x_tables import NORMAL_TABLE, NORMAL_PAIRS, ROM_BYTES


def synthetic_code():
    data=bytearray(ROM_BYTES)
    for pos,signature,_ in SELECTION_WITNESSES:
        b=bytes.fromhex(signature)
        data[pos:pos+len(b)]=b
    for slot,pointers in enumerate(NORMAL_PAIRS):
        for i,p in enumerate(pointers):
            pos=NORMAL_TABLE+8*slot+4*i
            data[pos:pos+4]=p.to_bytes(4,'big')
    return data

class SourceCourseIdentity(unittest.TestCase):
    def test_original_68000_selection_dataflow_and_names(self):
        report=verify_course_identity(synthetic_code(),verify_full_rom=False)
        self.assertTrue(report['source_index_to_original_course_order_verified'])
        self.assertFalse(report['original_racing_modes_completed'])
        self.assertEqual(report['selection_code_witnesses'],7)
        self.assertEqual(tuple(c['name'] for c in report['slots']),COURSE_NAMES)
        self.assertEqual(tuple(c['course_index'] for c in report['slots']),tuple(range(5)))
        self.assertTrue(all(c['source_68000_road_slot_verified'] for c in report['slots']))
        self.assertFalse(any(c['original_start_finish_verified'] for c in report['slots']))
        self.assertTrue(all(len(s)<=7 for s in COURSE_SHORT_NAMES))

    def test_rejects_broken_code_dataflow_and_wrong_source_bank(self):
        base=synthetic_code()
        for address,opcode,_ in SELECTION_WITNESSES:
            with self.subTest(address=hex(address)):
                broken=bytearray(base)
                broken[address]^=1
                with self.assertRaisesRegex(ValueError,'cannot prove'):
                    verify_course_identity(broken,verify_full_rom=False)
        broken=bytearray(base)
        broken[NORMAL_TABLE+8*3+4]^=1
        with self.assertRaisesRegex(ValueError,'data pointers differ'):
            verify_course_identity(broken,verify_full_rom=False)
        with self.assertRaisesRegex(ValueError,'incomplete'):
            verify_course_identity(bytes(500),verify_full_rom=False)
        # In production, malformed 68000 bytes NEVER bypass the full hash.
        with self.assertRaises(ValueError):
            verify_course_identity(base,verify_full_rom=True)

    def test_import_and_menu_use_verified_source_names(self):
        code=(ROOT/'tools/import_vrd_32x_explorer.py').read_text()
        self.assertIn('verify_course_identity(source,verify_full_rom=False)',code)
        self.assertIn('original_course_name=COURSE_NAMES[slot]',code)
        self.assertIn('original_competitive_races_complete=False',code)
        main=(ROOT/'src/main.c').read_text()
        for short in COURSE_SHORT_NAMES:
            self.assertIn('"'+short+'"',main)
        self.assertIn('"START UNVERIFIED"',main)

    def test_private_original_image_proves_index_to_road_selector(self):
        filename=os.environ.get('VR32X_ROM')
        if not filename:
            self.skipTest('Set VR32X_ROM for actual US original 32X source evidence')
        report=verify_course_identity(Path(filename).read_bytes())
        self.assertEqual([x['name'] for x in report['slots']],list(COURSE_NAMES))

if __name__=='__main__': unittest.main()
