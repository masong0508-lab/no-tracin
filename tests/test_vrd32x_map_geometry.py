"""Independent source-only checks for private 32X sampled map framing.

Every mask here is invented for this test; no Sega ROM bytes or generated
copyrighted course content are checked into the source repository.
"""
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
from vrd32x_map_geometry import sampled_preview_bits, sampled_road_bounds


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

    def test_fixed_preview_bitmap_keeps_sample_mask_gaps_and_orientation(self):
        mask = [False] * 64
        for row in range(8):
            for col in range(8):
                mask[row*8+col] = not (row<4 and col<4)
        bits = sampled_preview_bits(mask, (0, 255, 0, 255),
                                    side=8, units_per_sample=32)
        self.assertEqual(len(bits), 867)
        def pixel(x, y):
            i = y*102+x
            return bool(bits[i>>3] & (1 << (i&7)))
        # Source +Z is up; the genuine empty quadrant must stay lower-left.
        self.assertFalse(pixel(30, 50))
        self.assertTrue(pixel(30, 20))
        self.assertTrue(pixel(70, 50))
        self.assertTrue(pixel(70, 20))
        with self.assertRaises(ValueError):
            sampled_preview_bits(mask[:-1], (0,255,0,255),
                                 side=8, units_per_sample=32)

    def test_direct_68000_sampler_reveals_road_missed_by_coarse_mask(self):
        # An entirely empty coarse raster must not suppress a narrow true
        # source corridor when the authentic per-pixel BSP sampler sees it.
        coarse=[False]*64
        calls=[]
        def native(x,z):
            calls.append((x,z))
            return 100 <= x < 150 and 25 <= z < 225
        bounds=(80,160,0,255)
        bits=sampled_preview_bits(coarse,bounds,side=8,
                                  units_per_sample=32,source_sampler=native)
        self.assertEqual(len(bits),867)
        self.assertTrue(any(bits))
        self.assertGreater(len(calls),0)
        self.assertTrue(all(0<=x<256 and 0<=z<256 for x,z in calls))
        blank=sampled_preview_bits(coarse,bounds,side=8,units_per_sample=32)
        self.assertFalse(any(blank))
        empty_exact=sampled_preview_bits(coarse,bounds,side=8,
                                         units_per_sample=32,
                                         source_sampler=lambda x,z:False)
        self.assertFalse(any(empty_exact))

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
