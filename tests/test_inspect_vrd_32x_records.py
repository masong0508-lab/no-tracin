"""Detect actual 32X road-cell variable-record length, without game assets."""
import os
from pathlib import Path
import unittest
from tools.inspect_vrd_32x_records import inspect_records, record_boundaries
from tools.inspect_vrd_32x_tables import NORMAL_TABLE, ALTERNATE_TABLE


def fixture():
    raw = bytearray(0x110000)
    raw[NORMAL_TABLE:NORMAL_TABLE + 4] = (0x900800).to_bytes(4, "big")
    raw[NORMAL_TABLE + 4:NORMAL_TABLE + 8] = (0x904000).to_bytes(4, "big")
    # Two active cells start at word 0 and 15, followed by terminal 58.
    # N=0 -> 15 words, N=2 -> 43 words; marker N=0xffff.
    word_refs = [58] * 4096
    word_refs[2034] = 0
    word_refs[2035] = 15
    for i, word in enumerate(word_refs):
        raw[0x100800 + i * 2:0x100802 + i * 2] = word.to_bytes(2, "big")
    for offset, count in ((0, 0), (15, 2), (58, 0xffff)):
        raw[0x104000 + offset * 2:0x104002 + offset * 2] = count.to_bytes(2, "big")
    return raw


class RoadRecordTests(unittest.TestCase):
    def test_exact_counted_record_boundaries(self):
        s = record_boundaries(fixture(), NORMAL_TABLE, 0)
        self.assertEqual(s["record_count"], 2)
        self.assertEqual(s["subrecords_total"], 4)
        self.assertEqual(s["end_marker_word"], 65535)
        self.assertTrue(s["record_structure_confirmed"])
        self.assertEqual(s["source_data_start_file_offset"], 0x104000)
        self.assertEqual(s["source_data_end_file_offset"], 0x104076)

    def test_reject_single_record_count_mutation(self):
        raw = fixture()
        raw[0x10401e:0x104020] = (1).to_bytes(2, "big")
        s = record_boundaries(raw, NORMAL_TABLE, 0)
        self.assertEqual(s["boundary_errors"], 1)
        self.assertFalse(s["record_structure_confirmed"])

    def test_reject_nonterminal_marker(self):
        raw = fixture()
        raw[0x104074:0x104076] = (0).to_bytes(2, "big")
        s = record_boundaries(raw, NORMAL_TABLE, 0)
        self.assertFalse(s["record_structure_confirmed"])

    def test_verified_private_source(self):
        path = os.environ.get("VR32X_ROM")
        if not path:
            self.skipTest("Set VR32X_ROM to verify actual Deluxe record streams")
        path = Path(path)
        before_time = path.stat().st_mtime_ns
        before = path.read_bytes()
        result = inspect_records(before)
        self.assertEqual(result["fully_parsed_normal_slots"], [0, 1, 2, 3, 4])
        self.assertEqual(result["verified_record_count"], 1770)
        self.assertEqual([x["record_count"] for x in result["normal"]],
                         [294, 334, 316, 253, 558, 118])
        self.assertEqual(result["alternate"]["record_count"], 15)
        self.assertTrue(result["alternate"]["record_structure_confirmed"])
        for rec in result["normal"][:5]:
            self.assertEqual(rec["boundary_errors"], 0)
            self.assertEqual(rec["end_marker_word"], 65535)
        self.assertFalse(result["normal"][5]["record_structure_confirmed"])
        self.assertFalse(result["height_and_vertex_semantics_recovered"])
        self.assertEqual(path.read_bytes(), before)
        self.assertEqual(path.stat().st_mtime_ns, before_time)


if __name__ == "__main__":
    unittest.main()
