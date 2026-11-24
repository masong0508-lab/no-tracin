"""Exact native-road renderer occupancy gate, CPU work reduction, no missing asphalt.

All non-donor fixtures invented for this test. No commercial 32X ROM or
native derived assets are ever committed to GitHub.
"""
import os
from pathlib import Path
import random
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.vrd32x_render_candidates import raster_road_candidates


def bit(bitmap,x,z):
    i=z*256+x
    return bool(bitmap[i>>3]&(1<<(i&7)))


class OriginalRoadRasterGate(unittest.TestCase):
    def test_entire_64_unit_native_quad_requires_at_least_three_valid_corners(self):
        for side in (512,1024):
            stride=side//256
            normal=[None]*(side*side)
            gate=raster_road_candidates(normal,side)
            self.assertEqual(len(gate),8192)
            self.assertFalse(any(gate))
            # First three valid corners on an invented source road patch.
            for ix,iz in ((0,0),(stride,0),(stride,stride)):
                normal[iz*side+ix]=1234
            gate=raster_road_candidates(normal,side)
            self.assertTrue(bit(gate,0,0))
            self.assertFalse(bit(gate,1,0))
            self.assertFalse(bit(gate,255,255))
            # A missing corner on the opposite layer is still drivable;
            # the actual source height is selected later by car altitude.
            upper=[None]*(side*side)
            upper[0]=1234
            normal[0]=None
            union=raster_road_candidates(normal,side,upper)
            self.assertTrue(bit(union,0,0))
            self.assertEqual(len(union),8192)

    def test_exact_render_eligibility_vs_original_runtime_corner_rule(self):
        rng=random.Random(65302)
        for side in (512,1024):
            step=side//256
            for alternate in (False,True):
                samples=[None]*(side*side)
                other=[None]*(side*side) if alternate else None
                # Contrasting sparse patches, including exact road edges.
                for i in range(0,side*side,max(1,step*11)):
                    if rng.randrange(6)==0:samples[i]=rng.randrange(10,5000)
                    if other is not None and rng.randrange(8)==0:
                        other[i]=rng.randrange(10,5000)
                got=raster_road_candidates(samples,side,other)
                for z in range(256):
                    iz=z*step
                    for x in range(256):
                        ix=x*step
                        n=0
                        for px,pz in ((ix,iz),(ix,iz+step),
                                      (ix+step,iz+step),(ix+step,iz)):
                            if 0<=px<side and 0<=pz<side:
                                pos=pz*side+px
                                n+=bool(samples[pos] is not None or
                                        (other is not None and other[pos] is not None))
                        self.assertEqual(bit(got,x,z),n>=3,
                                         (side,alternate,x,z,n))

    def test_invalid_density_or_alternate_data_fails_closed(self):
        with self.assertRaises(ValueError):
            raster_road_candidates([None]*300,512)
        with self.assertRaises(ValueError):
            raster_road_candidates([None]*(512*512),512,[None]*128)

    def test_source_program_culls_before_render_and_never_alters_physics(self):
        renderer=(ROOT/'src/vrd32x_study.c').read_text()
        importer=(ROOT/'tools/import_vrd_32x_explorer.py').read_text()
        before=renderer.index('vrd_native_raster_candidates[slot][rc>>3]')
        after=renderer.index('if(!r_visible(x+32,z+32,66))continue;',before)
        self.assertLess(before,after)
        self.assertIn('raster_road_candidates(samples, SIDE, bridge_samples)',importer)
        self.assertIn('original_road_render_candidate_rom_bytes',importer)
        self.assertIn('return sample(slot,x,z) >= 0 ? SURF_ROAD : SURF_GRASS;',renderer)
        self.assertIn('vrd32x_study_set_height_reference',renderer)


if __name__=='__main__':unittest.main()
