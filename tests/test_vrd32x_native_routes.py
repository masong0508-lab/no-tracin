"""Original ordered 68000 road coordinate loops; no source bytes distributed."""
from pathlib import Path
import os
import sys
import unittest
from math import pi, cos, sin
# CI deliberately installs only Python stdlib. The original private importer
# uses NumPy, but our source-route correctness tests must NOT require it.
class SyntheticDistanceGrid:
    shape=(1024,1024)
    def __getitem__(self,point):
        row,col=point
        if not (0<=row<1024 and 0<=col<1024):
            raise IndexError(point)
        return 3


ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from vrd32x_native_routes import (COURSE_SCENE_TABLE, GAME_WORLD, MAX_NATIVE_STEP,
                                   native_to_gba, ordered_source_points,
                                   original_source_route)
from vrd32x_course_identity import COURSE_NAMES
from inspect_vrd_32x_tables import ROM_SHA256
from decode_vrd_32x_surface import RoadSurface


def synthetic_loop(count=64):
    # Artificial 64-node circle with native coordinates and the same loop
    # closure/separated auxiliary source record structure as the cartridge.
    points=[(round(3000*cos(i*2*pi/count)),round(3000*sin(i*2*pi/count)))
            for i in range(count)]
    points.append((3100,0))  # within source-native 512-unit finish gap
    points.append((-8000,-9000))  # separate source auxiliary branch
    image=bytearray(0x110000)
    ptr=0x900000
    image[COURSE_SCENE_TABLE:COURSE_SCENE_TABLE+4]=ptr.to_bytes(4,'big')
    at=ptr-0x800000
    for i,(x,z) in enumerate(points):
        image[at+i*4:at+i*4+4]=x.to_bytes(2,'big',signed=True)+z.to_bytes(2,'big',signed=True)
    return image


class OriginalRouteSourceTests(unittest.TestCase):
    def test_exact_source_index_loop_extraction_and_reject_discontinuity(self):
        b=synthetic_loop()
        points,audit=ordered_source_points(b,0,verified=False)
        self.assertEqual(len(points),65)
        self.assertTrue(audit['native_road_order_verified'])
        self.assertFalse(audit['original_start_finish_verified'])
        self.assertEqual(points[0],(3000,0))
        self.assertEqual(points[-1],(3100,0))
        self.assertEqual(native_to_gba(-16384),0)
        self.assertEqual(native_to_gba(16383),16383)
        with self.assertRaises(ValueError):native_to_gba(16384)
        b[0x100000+4*30:0x100000+4*30+4]=bytes.fromhex('f000 3000')
        with self.assertRaisesRegex(ValueError,'discontinuity'):
            ordered_source_points(b,0,verified=False)

    def test_authentic_source_route_into_gba_uses_native_coordinates(self):
        class Flat:
            def sample_world(self,x,z):
                return type('Hit',(),{'height':1000})()
        d=SyntheticDistanceGrid()
        points,lap,audit=original_source_route(synthetic_loop(),0,Flat(),None,
                                                500,d,None,pitch=16)
        self.assertEqual(len(points),65)
        self.assertGreater(lap,8000)
        self.assertLess(lap,10000)
        self.assertEqual(points[0]['x'],(3000+16384)//2)
        self.assertEqual(points[0]['z'],16384//2)
        self.assertEqual(points[0]['y'],int((1000-500)/32)+12)
        self.assertEqual(points[-1]['dist']+points[-1]['len'],lap)
        self.assertTrue(audit['GBA_u8_route_point_budget_satisfied'])
        self.assertFalse(audit['race_start_finish_verified'])

    def test_importer_must_not_substitute_skeleton_for_original(self):
        code=(ROOT/'tools/import_vrd_32x_explorer.py').read_text()
        self.assertIn('original_source_route(',code)
        self.assertIn('authentic_ordered_main_road_geometry_verified=True',code)
        self.assertIn('original_start_finish_verified=False',code)
        self.assertNotIn('route,lap,minimum=make_route(samples,mask,slot,distmap)',code)

    def test_owner_original_five_source_main_loops(self):
        path=os.environ.get('VR32X_ROM')
        if not path:self.skipTest('Set VR32X_ROM for original cartridge road geometry')
        image=Path(path).read_bytes()
        import hashlib
        self.assertEqual(hashlib.sha256(image).hexdigest(),ROM_SHA256)
        counts=[163,199,235,203,191]
        for slot in range(5):
            with self.subTest(course=COURSE_NAMES[slot]):
                nodes,audit=ordered_source_points(image,slot)
                self.assertEqual(len(nodes),counts[slot])
                self.assertEqual(audit['original_course_name'],COURSE_NAMES[slot])
                self.assertLessEqual(audit['native_source_closing_gap'],512)
                self.assertGreater(audit['native_raw_source_arc_length_estimate'],40000)
                r=RoadSurface(image,slot,verify_identity=False)
                alt=RoadSurface(image,slot,True,verify_identity=False) if slot==3 else None
                alt_count=0
                for i,(x,z) in enumerate(nodes):
                    h=r.sample_world(x,z)
                    h_alt=alt.sample_world(x,z) if alt else None
                    self.assertTrue(h or h_alt,(slot,i,x,z))
                    alt_count+=bool(h_alt)
                self.assertGreater(alt_count,10) if slot==3 else self.assertEqual(alt_count,0)

if __name__=='__main__':unittest.main()
