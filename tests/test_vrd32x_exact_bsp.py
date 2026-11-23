"""Original 68000 BSP geometry equivalence without distributing Sega assets.

Synthetic groups exercise every selector branch, strict signed thresholds,
negative slopes and first-match priority. The private, verified US donor
optionally cross-checks real counted road groups; source CI skips that part.
"""
import os
from pathlib import Path
import random
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from decode_vrd_32x_surface import RoadSurface, test_bsp_group
from inspect_vrd_32x_tables import ROM_SHA256, inspect_image
from export_vrd32x_exact_bsp import vector_svg
from vrd32x_exact_bsp import (audit_mode_group_identity, contains,
                              clipped_polygon, first_group, group_halfplanes,
                              mode_polygon_bounds, signed_area, tile_regions)


def synthetic_group(branches):
    """4 tests of (axis, direct, polarity, coefficient/threshold, constant)."""
    assert len(branches) == 4
    selector = 0
    pairs = []
    for i, (axis, direct, polarity, value, constant) in enumerate(branches):
        selector |= ((int(bool(axis)) << 2) | (int(bool(direct)) << 1)
                     | int(bool(polarity))) << (13 - 3 * i)
        pairs.extend((value, constant))
    # 2-byte selector, four signed pairs, and 10 bytes of plane/metadata.
    result = struct.pack(">H8h5h", selector, *pairs, *([0] * 5))
    assert len(result) == 28
    return result


# Strictly non-restricting original 68000 constraints: x_word < 1023.
ALL = (0, 1, 0, 1023, 0)


class ExactBspHalfplanes(unittest.TestCase):
    def test_every_branch_and_polarity_matches_68000_on_integer_grid(self):
        points = [(x, z) for x in range(0,512,31) for z in range(0,512,37)]
        for axis in (0, 1):
            for direct in (0, 1):
                for polarity in (0, 1):
                    for value, const in ((-32768, -500), (-513, 200),
                                         (0, 0), (513, -200),
                                         (32767, 32767)):
                        # Put branch under test first; allow remaining
                        # constraints to pass for arbitrary source points.
                        group = synthetic_group([(axis, direct, polarity, value, const),
                                                 ALL, ALL, ALL])
                        halfplanes = group_halfplanes(group, 0)
                        for x, z in points:
                            with self.subTest(axis=axis, direct=direct,
                                              polarity=polarity, value=value,
                                              const=const, x=x, z=z):
                                self.assertEqual(
                                    contains(halfplanes, x, z),
                                    test_bsp_group(group, 0, 2*x, 2*z))

    def test_four_mixed_constraints_randomized(self):
        randomizer = random.Random(40861)
        for iteration in range(300):
            branches = []
            for _ in range(4):
                axis = randomizer.randrange(2)
                direct = randomizer.randrange(2)
                polarity = randomizer.randrange(2)
                value = randomizer.randint(-3000, 3000)
                constant = randomizer.randint(-1000, 1000)
                branches.append((axis,direct,polarity,value,constant))
            group = synthetic_group(branches)
            hp = group_halfplanes(group, 0)
            for _ in range(42):
                x, z = randomizer.randrange(512), randomizer.randrange(512)
                self.assertEqual(contains(hp,x,z),
                                 test_bsp_group(group,0,x*2,z*2),
                                 (iteration,x,z,branches))
            poly = clipped_polygon(hp)
            for x,z in poly:
                self.assertGreaterEqual(x, 0)
                self.assertLessEqual(x, 511)
                self.assertGreaterEqual(z, 0)
                self.assertLessEqual(z, 511)
                for p in hp:
                    self.assertLessEqual(p.ax*x+p.az*z, p.bound)
            self.assertGreaterEqual(signed_area(poly),0)

    def test_first_valid_source_group_and_empty_tiles(self):
        group0 = synthetic_group([(0,1,0,500,0), ALL, ALL, ALL])
        group1 = synthetic_group([(0,1,1,500,0), ALL, ALL, ALL])
        stream = b"\x00\x01" + group0 + group1 + b"\xff\xff"
        class Source:
            rom = stream
            grid = [29] * (64*64)
            background = 29
            bank_start = 0
            limit_word = 29
            slot = 0

            def sample_local(self, row, col, x, z):
                if self.grid[row*64+col] == self.background:
                    return None
                for i in range(2):
                    if test_bsp_group(self.rom,2+28*i,2*x,2*z):
                        return type("Hit", (), {"group": i})()
                return None
        Source.grid[0] = 0
        regions = tile_regions(Source,0,0)
        self.assertEqual(len(regions),2)
        self.assertEqual(first_group(regions,100,80),0)
        self.assertEqual(first_group(regions,400,80),1)
        self.assertEqual(first_group(regions,-1,80),None)
        self.assertEqual(tile_regions(Source,0,1),())
        self.assertEqual(mode_polygon_bounds(Source), (0,320,0,320))
        audit = audit_mode_group_identity(Source,sample_points=(0,63,255,383,511))
        self.assertTrue(audit["first_match_source_parity"])
        self.assertEqual(audit["sample_points_verified"],25)
        self.assertEqual(audit["active_source_tiles"],1)
        svg, stats = vector_svg(Source)
        self.assertIn("source BSP road footprints",svg)
        self.assertIn('viewBox="0 0 16384 16384"',svg)
        self.assertEqual(svg.count("<polygon points="),2)
        self.assertEqual(stats["nondegenerate_vector_footprints"],2)
        self.assertFalse(stats["named_deluxe_course_verified"])
        with self.assertRaises(ValueError):
            tile_regions(Source,64,0)

    def test_truncated_or_invalid_geometry_fails_closed(self):
        with self.assertRaises(ValueError):
            group_halfplanes(b"\x00"*27,0)
        with self.assertRaises(ValueError):
            clipped_polygon((),side=513)


class PrivateOriginalBspEquivalence(unittest.TestCase):
    def test_verified_32x_cartridge_groups_match_exact_rational_model(self):
        filename = os.environ.get("VR32X_ROM")
        if not filename:
            self.skipTest("Set VR32X_ROM to verify original private ROM BSP polygons")
        source = Path(filename).read_bytes()
        self.assertEqual(__import__("hashlib").sha256(source).hexdigest(), ROM_SHA256)
        inspect_image(source,require_identity=True)
        for slot in range(5):
            road = RoadSurface(source,slot,verify_identity=False)
            active = [i for i,v in enumerate(road.grid) if v != road.background]
            self.assertGreater(len(active),100)
            # Distributed representative tiles (the source sampler is
            # checked for group identity, not just whether any road exists).
            chosen = [active[i] for i in sorted({
                0,1,len(active)//5,2*len(active)//5,3*len(active)//5,
                4*len(active)//5,len(active)-1
            })]
            for index in chosen:
                row,col = divmod(index,64)
                regions = tile_regions(road,row,col)
                for z in (0,1,32,64,128,256,384,448,510,511):
                    for x in (0,1,32,64,128,256,384,448,510,511):
                        expected = road.sample_local(row,col,x,z)
                        self.assertEqual(first_group(regions,x,z),
                                         expected.group if expected else None,
                                         (slot,row,col,x,z))


if __name__ == "__main__":
    unittest.main()
