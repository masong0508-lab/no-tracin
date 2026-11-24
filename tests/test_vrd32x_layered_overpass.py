"""Highland: independent native 68000 normal/alternate collision layers.

No Sega data in public fixtures. Optionally checks source route using the
exact owner ROM when VR32X_ROM is set. Production-C functions are compiled
with a deliberately synthetic two-store 16-samples-per-tile fixture.
"""
import os
import pathlib
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]


class NativeSourceOverpassTests(unittest.TestCase):
    def test_production_collision_preserves_over_and_under_at_same_coordinates(self):
        cc=shlex.split(os.environ.get('HOST_CC','cc'))
        if not cc or not shutil.which(cc[0]):
            self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory(prefix='ntr-overpass-') as work:
            d=pathlib.Path(work)
            gba=(ROOT/'src/gba.h').read_text()
            self.assertEqual(gba.count('target("arm"), '),1)
            (d/'gba_host.h').write_text(gba.replace('target("arm"), ',''))
            (d/'vrd32x_study_data.h').write_text('''
// ALL SOURCE DATA BELOW IS SYNTHETIC, NOT FROM THE COMMERCIAL 32X ROM.
#define VRD_SAMPLE_SHIFT 4
#define VRD_TILE_SHIFT 4
#define VRD_TILE_AREA 256
#define VRD_HAS_NATIVE_OVERPASS 1
static const u16 vrd_native_bridge_bounds[4]={0,511,0,511};
static const u16 lower_grid[4096] = { [0]=1 };
static const u16 lower_heights[256] = { [0 ... 255]=72 };
static const u16* const vrd_study_lookups[5] = {lower_grid,lower_grid,lower_grid,lower_grid,lower_grid};
static const u16* const vrd_study_heights[5] = {lower_heights,lower_heights,lower_heights,lower_heights,lower_heights};
static const u16 upper_grid[4096] = { [0]=1, [1]=2 };
static const u16 upper_heights[512] = { [0 ... 255]=151, [256 ... 511]=161 };
#define vrd_native_overpass_lookup upper_grid
#define vrd_native_overpass_heights upper_heights
static const u8 blank[867]={0};
static const u8 *const vrd_study_preview_bits[5]={blank,blank,blank,blank,blank};
static const u16 vrd_study_preview_bounds[5][4]={{0,511,0,511},{0,511,0,511},{0,511,0,511},{0,511,0,511},{0,511,0,511}};
''')
            (d/'check.c').write_text(r'''
#include <assert.h>
#include "track.h"
#include "world.h"
#include "vrd32x_study.h"
const TrackDef g_tracks[TRACK_COUNT]={0};
const TrackDef *g_track=0;
int main(void){
    s32 h,gx,gz;
    g_track=g_tracks+TRACK_VRD_FIRST+3;
    assert(vrd32x_study_slot()==3);
    // At x=96,z=96, both distinct source layers have valid collision.
    vrd32x_study_set_height_reference(72*256);
    assert(vrd32x_study_height(3,96,96)==72*256);
    assert(vrd32x_study_surface(3,96,96)==SURF_ROAD);
    assert(vrd32x_study_ground_plane(3,96,96,8,&h,&gx,&gz));
    assert(h==72*256 && gx==0 && gz==0);
    vrd32x_study_set_height_reference(151*256);
    assert(vrd32x_study_height(3,96,96)==151*256);
    assert(vrd32x_study_ground_plane(3,96,96,8,&h,&gx,&gz));
    assert(h==151*256 && gx==0 && gz==0);
    // Other source modes never gain native overpass data.
    assert(vrd32x_study_height(2,96,96)==72*256);
    // Only upper has a surface in tile 1: don't erase elevated road.
    vrd32x_study_set_height_reference(72*256);
    assert(vrd32x_study_height(3,288,96)==161*256);
    assert(vrd32x_study_surface(3,288,96)==SURF_ROAD);
    assert(vrd32x_study_surface(2,288,96)==SURF_GRASS);
    assert(vrd32x_study_height(3,512,96)==0);
    assert(vrd32x_study_surface(3,16384,96)==SURF_GRASS);
    return 0;
}
''')
            command=cc+['-std=gnu99','-O2','-Wno-pointer-to-int-cast',
              '-ffunction-sections','-fdata-sections','-fsanitize=shift,signed-integer-overflow',
              '-fno-sanitize-recover=all','-include',str(d/'gba_host.h'),
              '-DWITH_VRD_32X_TRACKS','-I',str(d),'-I',str(ROOT/'src'),
              '-Wl,--gc-sections',str(d/'check.c'),str(ROOT/'src/vrd32x_study.c'),
              '-o',str(d/'check')]
            built=subprocess.run(command,capture_output=True,text=True,timeout=30)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(d/'check')],capture_output=True,text=True,timeout=15)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)

    def test_donor_alternate_road_makes_a_two_height_long_cycle(self):
        filename=os.environ.get('VR32X_ROM')
        if not filename:self.skipTest('VR32X_ROM needed for private original-data proof')
        import numpy as np
        from tools.decode_vrd_32x_surface import RoadSurface
        from tools.export_vrd_32x_surface_mesh import grid_samples
        from tools.vrd32x_layered_course import layered_main_cycle
        from tools.inspect_vrd_32x_tables import ROM_SHA256
        import hashlib
        binary=pathlib.Path(filename).read_bytes()
        self.assertEqual(hashlib.sha256(binary).hexdigest(),ROM_SHA256)
        base=RoadSurface(binary,3,verify_identity=False)
        bridge=RoadSurface(binary,3,True,verify_identity=False)
        side,lo=grid_samples(base,16)
        _,hi=grid_samples(bridge,16)
        mask0=np.array([x is not None for x in lo],dtype=bool).reshape((side,side))
        mask1=np.array([x is not None for x in hi],dtype=bool).reshape((side,side))
        overlapping=[(a,b) for a,b in zip(lo,hi) if a is not None and b is not None]
        self.assertGreater(len(overlapping),100)
        self.assertGreater(max(abs(a-b) for a,b in overlapping),1000)
        route,report=layered_main_cycle(mask0,mask1,lo,hi,world_per_pixel=16)
        self.assertGreater(report['gba_world_route_length_estimate'],20000)
        self.assertGreater(report['source_bridge_points'],10)
        self.assertEqual(len(report['bridge_transitions']),2)
        self.assertEqual({n[0] for n in route},{0,1})
        self.assertFalse(report['original_start_finish_verified'])


if __name__=='__main__':unittest.main()

# Real production car_reset/car_respawn and bridge reference contract.
# Tests are deliberately local and deterministic, no original Sega assets.
class NativeBridgeRecoveryTests(unittest.TestCase):
    def test_crash_respawns_on_saved_upper_layer_not_lower_underpass(self):
        cc=shlex.split(os.environ.get('HOST_CC','cc'))
        if not cc or not shutil.which(cc[0]):
            self.skipTest('Host compiler unavailable')
        with tempfile.TemporaryDirectory(prefix='ntr-native-respawn-') as temp:
            work=pathlib.Path(temp)
            hdr=(ROOT/'src/gba.h').read_text()
            self.assertEqual(hdr.count('target("arm"), '),1)
            (work/'gba_host.h').write_text(hdr.replace('target("arm"), ',''))
            (work/'check.c').write_text(r'''
#include <assert.h>
#include "car.h"
#include "track.h"
#include "world.h"
#include "vrd32x_study.h"
static s32 ref_height;
static int reset_observed;
const TrackDef g_tracks[TRACK_COUNT]={0};
const TrackDef *g_track=&g_tracks[TRACK_VRD_FIRST+3];
s32 vrd32x_study_slot(void){return 3;}
void vrd32x_study_set_height_reference(s32 v){ref_height=v;}
s32 world_height(s32 x,s32 z) {
  assert(x==5000 && z==8000);
  reset_observed++;
  return ref_height < 100*256 ? 72*256 : 151*256;
}
void sb_invalidate(void){}
void race_on_respawn(Car *c){(void)c;}
int main(void){
  Car c={0};
  vrd32x_study_set_height_reference(151*256);
  car_reset(&c,5000,8000,12000);
  assert(c.y==151*256 && c.safe_y==151*256);
  assert(reset_observed==1);
  // Falling to the LOWER original layer must not change safe altitude.
  c.y=72*256;
  vrd32x_study_set_height_reference(c.y);
  car_respawn(&c);
  assert(c.y==151*256 && c.safe_y==151*256);
  assert(c.safe_x==5000 && c.safe_z==8000 && c.heading==12000);
  // Lower-road respawn must still remain on the original lower surface.
  vrd32x_study_set_height_reference(72*256);
  car_reset(&c,5000,8000,5000);
  c.y=151*256;
  vrd32x_study_set_height_reference(c.y);
  car_respawn(&c);
  assert(c.y==72*256 && c.safe_y==72*256 && c.heading==5000);
  return 0;
}
''')
            args=cc+['-std=c99','-O2','-ffunction-sections','-fdata-sections',
                     '-Wno-pointer-to-int-cast','-fsanitize=shift,signed-integer-overflow',
                     '-fno-sanitize-recover=all','-include',str(work/'gba_host.h'),
                     '-DWITH_VRD_32X_TRACKS','-I',str(ROOT/'src'),
                     '-Wl,--gc-sections',str(work/'check.c'),str(ROOT/'src/car.c'),
                     '-o',str(work/'check')]
            p=subprocess.run(args,text=True,capture_output=True,timeout=30)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            p=subprocess.run([str(work/'check')],text=True,capture_output=True,timeout=10)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
