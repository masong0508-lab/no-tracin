"""Original 32X road-anchored terrain and non-flat GBA scene regressions.

All fixture pixels and scenery are synthetic. Donor source ROM stays private.
The offline generator tests skip gracefully in source-only CI if scientific
Python is unavailable; production-C visual path remains independently tested.
"""
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SCIENTIFIC=(importlib.util.find_spec('numpy') is not None and
            importlib.util.find_spec('scipy') is not None)


class NativeTerrainSourceTests(unittest.TestCase):
    def test_road_scenery_is_not_the_previous_flat_checkerboard(self):
        code=(ROOT/'src/vrd32x_study.c').read_text()
        self.assertIn('#ifdef VRD_NATIVE_SCENE',code)
        self.assertIn('draw_native_source_road(slot)',code)
        self.assertIn('terrain_vertex(slot',code)
        self.assertIn('vrd_native_ribbon_skip[slot]',code)
        self.assertIn('vrd_native_scenery[slot]',code)
        self.assertNotIn('COLOR(M_GRASS,(ix+iz)&1)',code)
        importer=(ROOT/'tools/import_vrd_32x_explorer.py').read_text()
        self.assertIn('terrain_from_source(',importer)
        self.assertIn('original_source_route(',importer)
        self.assertIn("data.append('#define VRD_NATIVE_SCENE 1')",importer)
        compile(importer,str(ROOT/'tools/import_vrd_32x_explorer.py'),'exec')
        compile((ROOT/'tools/vrd32x_visual_terrain.py').read_text(),
                str(ROOT/'tools/vrd32x_visual_terrain.py'),'exec')

    @unittest.skipUnless(SCIENTIFIC,'requires NumPy/SciPy for user-private terrain synthesis')
    def test_continuous_offroad_relief_and_course_specific_placement(self):
        from tools.vrd32x_visual_terrain import terrain_from_source, ribbon_overdraw_skip
        side=512
        src=[None]*(side*side)
        for z in range(175,330):
            for x in range(175,330):
                src[z*side+x]=3500+x//5+z//11
        route=[]
        for x,z in ((7500,8000),(7700,8000),(7900,8000),
                    (7900,8200),(7700,8200),(7500,8200)):
            route.append(dict(x=x,z=z,y=40,ux=16384,uz=0,half_width=150))
        seen=[]
        for slot in range(5):
            hs,cover,props=terrain_from_source(src,side,0,slot,route)
            self.assertEqual(len(hs),65*65)
            self.assertEqual(len(cover),64*64)
            self.assertGreater(len(set(hs)),30)
            self.assertGreater(len(set(cover)),1)
            self.assertTrue(all(0<=y<=780 for y in hs))
            self.assertTrue(all(0<=m<=5 for m in cover))
            self.assertGreater(len(props),3)
            for x,y,z,ux,uz,kind,size in props:
                self.assertTrue(0<=x<16384 and 0<=z<16384)
                self.assertTrue(-16384<=ux<=16384 and -16384<=uz<=16384)
                self.assertTrue(0<=y<=780)
            seen.append(tuple(cover))
        self.assertGreater(len(set(seen)),3)
        packed=ribbon_overdraw_skip(route)
        self.assertEqual(len(packed),8192)
        self.assertTrue(any(packed))
        self.assertFalse(packed[0] & 1)  # far offroad stays sourced raster

    def test_production_32x_visual_path_compiles_and_draws_contoured_ground(self):
        cc=shlex.split(os.environ.get('HOST_CC','cc'))
        if not cc or not shutil.which(cc[0]):
            self.skipTest('host C compiler unavailable')
        with tempfile.TemporaryDirectory(prefix='ntr-32x-visual-') as d:
            folder=Path(d)
            gba=(ROOT/'src/gba.h').read_text()
            self.assertEqual(gba.count('target("arm"), '),1)
            (folder/'gba_host.h').write_text(gba.replace('target("arm"), ',''))
            (folder/'vrd32x_study_data.h').write_text(r'''
// Completely invented 16-sample original-source-raster fixture.
#define VRD_NATIVE_SCENE 1
#define VRD_SAMPLE_SHIFT 4
#define VRD_TILE_SHIFT 4
#define VRD_TILE_AREA 256
static const u16 grid[4096]={[0]=1};
static const u16 heights[256]={[0 ... 255]=25};
static const u16 *const vrd_study_lookups[5]={grid,grid,grid,grid,grid};
static const u16 *const vrd_study_heights[5]={heights,heights,heights,heights,heights};
static const u16 vb[5][4]={{0,1024,0,1024},{0,1024,0,1024},{0,1024,0,1024},{0,1024,0,1024},{0,1024,0,1024}};
#define vrd_study_preview_bounds vb
static const u8 bits[867]={0};
static const u8 *const vrd_study_preview_bits[5]={bits,bits,bits,bits,bits};
static const u16 terrain[4225]={[0 ... 4224]=42};
static const u16 *const vrd_native_terrain_height[5]={terrain,terrain,terrain,terrain,terrain};
static const u8 cover[4096]={[0 ... 4095]=4};
static const u8 *const vrd_native_terrain_cover[5]={cover,cover,cover,cover,cover};
static const u8 skip[8192]={0};
static const u8 *const vrd_native_ribbon_skip[5]={skip,skip,skip,skip,skip};
// The exact import-time quad gate allows every synthetic surface cell.
// It must be present in source-only ROM-free production C tests.
static const u8 candidates[8192]={[0 ... 8191]=255};
static const u8 *const vrd_native_raster_candidates[5]={candidates,candidates,candidates,candidates,candidates};
static const VrdScenery props[1]={{520,42,520,16384,0,0,80}};
static const VrdScenery *const vrd_native_scenery[5]={props,props,props,props,props};
static const u8 vrd_native_scenery_count[5]={1,1,1,1,1};
''')
            (folder/'probe.c').write_text(r'''
#include <assert.h>
#include "track.h"
#include "world.h"
#include "vrd32x_study.h"
static const TrackPt line[4]={
    {.x=480,.z=480,.y=26,.uz=16384,.len=128},
    {.x=480,.z=608,.y=26,.uz=16384,.len=128},
    {.x=608,.z=608,.y=26,.ux=16384,.len=128},
    {.x=608,.z=480,.y=26,.ux=16384,.len=128},
};
static const u16 widths[4]={90,90,90,90};
const TrackDef g_tracks[TRACK_COUNT]={
    [TRACK_VRD_FIRST]={.pts=line,.count=4,.half_widths=widths}
};
const TrackDef *g_track=g_tracks+TRACK_VRD_FIRST;
static int terrain_drawn,road_drawn,tree_drawn;
s32 r_visible(s32 x,s32 z,s32 r){(void)x;(void)z;(void)r;return 1;}
s32 r_depth(s32 x,s32 z,s32 *side){(void)x;(void)z;*side=0;return 100;}
s32 r_faces_free(void){return 250;}
s32 isin(s32 a){(void)a;return 0;}
s32 icos(s32 a){(void)a;return 16384;}
void r_ground(const Vec3 *q,s32 n,u8 c){
 (void)c;
 if(n==4&&q[0].y==42&&q[1].y==42)terrain_drawn++;
 if(n==4&&q[0].y==29)road_drawn++;
}
void r_face(const Vec3 *v,s32 n,u8 c,u32 f){
 (void)v;(void)n;(void)c;(void)f;tree_drawn++;
}
void r_box(s32 a,s32 b,s32 c,s32 d,s32 e,s32 f,s32 g){
 (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;
 tree_drawn++;
}
int main(void){
 assert(vrd32x_study_slot()==0);
 assert(vrd32x_study_height(0,100,100)==25*256);
 vrd32x_study_draw(0,512,512,0);
 assert(terrain_drawn>=15);
 assert(road_drawn>=1);
 assert(tree_drawn>=2);
 return 0;
}
''')
            command=cc+['-std=gnu99','-O2','-Wno-pointer-to-int-cast',
                '-ffunction-sections','-fdata-sections',
                '-fsanitize=shift,signed-integer-overflow','-fno-sanitize-recover=all',
                '-include',str(folder/'gba_host.h'),'-DWITH_VRD_32X_TRACKS',
                '-I',str(folder),'-I',str(ROOT/'src'),'-Wl,--gc-sections',
                str(folder/'probe.c'),str(ROOT/'src/vrd32x_study.c'),'-o',str(folder/'probe')]
            compiled=subprocess.run(command,capture_output=True,text=True,timeout=40)
            self.assertEqual(compiled.returncode,0,compiled.stdout+compiled.stderr)
            run=subprocess.run([str(folder/'probe')],capture_output=True,text=True,timeout=15)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)


if __name__=='__main__':
    unittest.main()
