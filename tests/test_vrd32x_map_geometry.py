"""Independent source-only checks for private 32X sampled map framing.

Every mask here is invented for this test; no Sega ROM bytes or generated
copyrighted course content are checked into the source repository.
"""
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
from vrd32x_map_geometry import sampled_road_bounds


class SampledRoadBoundsTests(unittest.TestCase):
    def test_exact_extents_and_margin(self):
        mask=[False]*64
        mask[3*8+2]=True
        mask[6*8+5]=True
        self.assertEqual(sampled_road_bounds(mask,side=8,units_per_sample=32,margin=0),
                         (64,191,96,223))
        self.assertEqual(sampled_road_bounds(mask,side=8,units_per_sample=32,margin=1),
                         (32,223,64,255))

    def test_clips_at_source_world_edges(self):
        mask=[False]*64
        mask[0]=True
        mask[-1]=True
        self.assertEqual(sampled_road_bounds(mask,side=8,units_per_sample=32,margin=5),
                         (0,255,0,255))

    def test_thin_branch_does_not_invent_tile_wide_bounds(self):
        mask=[False]*64
        # Two isolated, source-authenticated points in one row.
        mask[4*8+3]=True
        mask[4*8+5]=True
        self.assertEqual(sampled_road_bounds(mask,side=8,units_per_sample=32,margin=0),
                         (96,191,128,159))

    def test_fails_closed_on_unsupported_masks(self):
        for mask in ([],[False]*64,[True]*63,[True]*65):
            with self.subTest(length=len(mask)):
                with self.assertRaises(ValueError):
                    sampled_road_bounds(mask,side=8,units_per_sample=32,margin=0)
        for settings in ({"side":1},{"units_per_sample":0},{"margin":-1}):
            with self.subTest(settings=settings), self.assertRaises(ValueError):
                sampled_road_bounds([True]*64,side=8 if "side" not in settings else settings["side"],
                                    units_per_sample=settings.get("units_per_sample",32),
                                    margin=settings.get("margin",0))


if __name__=="__main__":
    unittest.main()
