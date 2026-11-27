"""Regression checks for original 32X overlapping road layers."""
import itertools
import os
import random
import unittest
from pathlib import Path
from tools.vrd32x_route_layers import choose_closed_layers, source_route_layers


class RouteLayerTests(unittest.TestCase):
    def test_underpass(self):
        self.assertEqual(choose_closed_layers([(1,None),(2,100),(3,101),(4,None)]),[0,0,0,0])

    def test_forced_upper(self):
        self.assertEqual(choose_closed_layers([(0,None),(1,1),(None,2),(3,None)]),[0,0,1,0])

    def test_invalid(self):
        for samples in ([], [(1,2),(None,None)], [(1,)]):
            with self.assertRaises(ValueError):
                choose_closed_layers(samples)

    def test_exhaustive_closed_loop(self):
        rng = random.Random(2026)
        for _ in range(250):
            hs = [(rng.randint(-10,10),rng.randint(-10,10)) for _ in range(7)]
            for i in range(7):
                if rng.randrange(3)==0:
                    hs[i]=(hs[i][0],None)
            def cost(p):
                return (sum(abs(hs[i][p[i]]-hs[(i+1)%len(p)][p[(i+1)%len(p)]]) for i in range(len(p))),
                        sum(p[i]!=p[(i+1)%len(p)] for i in range(len(p))))
            expected=min(cost(p) for p in itertools.product((0,1),repeat=len(hs))
                         if all(hs[i][j] is not None for i,j in enumerate(p)))
            self.assertEqual(cost(choose_closed_layers(hs)),expected)

    def test_no_alternate_does_not_query(self):
        self.assertEqual(source_route_layers([(0,0),(1,0)],None,None),[0,0])

    def test_original_highland(self):
        path=os.environ.get('VR32X_ROM')
        if not path:
            self.skipTest('set VR32X_ROM to original owner cartridge')
        from tools.vrd32x_native_routes import ordered_source_points
        from tools.decode_vrd_32x_surface import RoadSurface
        image=Path(path).read_bytes()
        points,_=ordered_source_points(image,3)
        normal=RoadSurface(image,3)
        alternate=RoadSurface(image,3,alternate=True)
        layers=source_route_layers(points,normal,alternate)
        self.assertEqual(len(layers),203)
        self.assertEqual(layers[120],0)
        self.assertGreater(sum(layers),0)
        self.assertEqual([i for i,p in enumerate(points)
                          if layers[i]!=int(alternate.sample_world(*p) is not None)],
                         [120,176,177,186,187])
