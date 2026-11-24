"""Regression for cartridge-ROM ribbon geometry and conservative draw culling.

All fixture route points are invented (no commercial Sega data). Exact
source-route quads are rebuilt from the same integer math as prior C code.
"""
import math
from pathlib import Path
import random
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.vrd32x_ribbon_mesh import (GROUP_SIZE,original_ribbon_quads,
    conservative_visibility_groups,group_culling_never_drops_visible_child,
    visibility)


def invented_route(count=48):
    route=[]
    for i in range(count):
        angle=math.tau*i/count
        nxt=math.tau*(i+1)/count
        x=round(8000+2000*math.sin(angle))
        z=round(8000+1500*math.cos(angle))
        xn=round(8000+2000*math.sin(nxt))
        zn=round(8000+1500*math.cos(nxt))
        length=max(1,round(math.hypot(xn-x,zn-z)))
        ux=round((xn-x)*16384/length)
        uz=round((zn-z)*16384/length)
        route.append(dict(x=x,z=z,y=30+(i%19),ux=ux,uz=uz,
                          len=length,half_width=33+(i*11)%145))
    return route


class ExactSourceRibbonTests(unittest.TestCase):
    def test_quads_match_old_GBA_signed_shift_and_width_limits(self):
        route=invented_route(48)
        quads=original_ribbon_quads(route)
        self.assertEqual(len(quads),len(route))
        for i,(a,q) in enumerate(zip(route,quads)):
            b=route[(i+1)%len(route)]
            wa=max(14,min(110,a['half_width']*3//4))
            wb=max(14,min(110,b['half_width']*3//4))
            expected=(
                a['x']-((a['uz']*wa)>>14),a['y']+3,a['z']+((a['ux']*wa)>>14),
                b['x']-((b['uz']*wb)>>14),b['y']+3,b['z']+((b['ux']*wb)>>14),
                b['x']+((b['uz']*wb)>>14),b['y']+3,b['z']-((b['ux']*wb)>>14),
                a['x']+((a['uz']*wa)>>14),a['y']+3,a['z']-((a['ux']*wa)>>14),
            )
            self.assertEqual(q.xyz,expected)
            self.assertEqual(q.mx,(a['x']+b['x'])//2)
            self.assertEqual(q.mz,(a['z']+b['z'])//2)
            self.assertEqual(q.reach,180+a['len']//2)
            self.assertEqual(q.shade,int(abs(a['y']-b['y'])>36))
            self.assertEqual(q.short_mark,int(a['len']<350))

    def test_group_conservative_visibility_for_every_angle(self):
        quads=original_ribbon_quads(invented_route(64))
        groups=conservative_visibility_groups(quads)
        self.assertEqual(len(groups),8)
        for yaw in range(0,1024,7):
            angle=yaw*math.tau/1024
            sine=round(16384*math.sin(angle))
            cosine=round(16384*math.cos(angle))
            for x,z in ((8000,8000),(6000,6000),(9000,7000),(12000,8800),
                        (3000,10000),(20000,18000)):
                for far in (900,1500,1800,2400):
                    self.assertTrue(group_culling_never_drops_visible_child(
                        quads,groups,x,z,cosine,sine,far),
                        (yaw,x,z,far))
        rng=random.Random(35007)
        for _ in range(3000):
            yaw=rng.randrange(1024)
            angle=yaw*math.tau/1024
            self.assertTrue(group_culling_never_drops_visible_child(
                quads,groups,rng.randrange(4000,13000),
                rng.randrange(4000,13000),round(16384*math.cos(angle)),
                round(16384*math.sin(angle)),rng.randint(850,2400)))

    def test_culling_spares_far_road_segments_without_missing_visible(self):
        quads=original_ribbon_quads(invented_route(64))
        groups=conservative_visibility_groups(quads)
        yaw=384
        angle=yaw*math.tau/1024
        cosine=round(16384*math.cos(angle));sine=round(16384*math.sin(angle))
        origin=(7000,7000)
        active=[visibility(q.mx-origin[0],q.mz-origin[1],cosine,sine,q.reach,1500)
                for q in quads]
        group_ok=[visibility(g.mx-origin[0],g.mz-origin[1],cosine,sine,g.reach,1500)
                  for g in groups]
        self.assertTrue(any(active))
        self.assertTrue(any(group_ok))
        self.assertTrue(any(not v for v in group_ok))
        self.assertTrue(all(not ok or group_ok[i//GROUP_SIZE] for i,ok in enumerate(active)))

    def test_invalid_route_and_group_limits_are_guarded(self):
        with self.assertRaises(ValueError):original_ribbon_quads([])
        with self.assertRaises(ValueError):conservative_visibility_groups([])
        with self.assertRaises(ValueError):conservative_visibility_groups(
            original_ribbon_quads(invented_route()),group_size=0)
        invalid=invented_route()
        invalid[0]['x']=40000
        with self.assertRaises(ValueError):original_ribbon_quads(invalid)

    def test_private_header_is_only_data_and_reuses_current_original_routes(self):
        c=(ROOT/'src/vrd32x_study.c').read_text()
        importer=(ROOT/'tools/import_vrd_32x_explorer.py').read_text()
        self.assertIn('original_ribbon_quads(route)',importer)
        self.assertIn('conservative_visibility_groups(quads)',importer)
        self.assertIn('vrd_native_ribbon_quads[slot][i]',c)
        self.assertIn('if(!r_visible(mx,mz,q->reach))continue;',c)
        self.assertIn('if((i&7)==0 && !r_visible(',c)
        self.assertIn('#ifdef VRD_ROM_RIBBON_MESH',c)
        self.assertIn('#define VRD_ROM_RIBBON_MESH 1',importer)
        self.assertIn('return sample(slot,x,z) >= 0 ? SURF_ROAD : SURF_GRASS;',c)

if __name__=='__main__':unittest.main()
