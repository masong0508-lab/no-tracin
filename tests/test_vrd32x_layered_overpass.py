"""Native 68000 32X Highland normal/alternate overpass regression.

All compiled source-only arrays are invented. Owner ROM is loaded only
when VR32X_ROM explicitly identifies a private original cartridge.
"""
import os, pathlib, shlex, shutil, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]


class NativeOverpassTests(unittest.TestCase):
    def test_production_c_two_heights_and_upper_only_road(self):
        cc=shlex.split(os.environ.get("HOST_CC","cc"))
        if not cc or not shutil.which(cc[0]):
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="ntr-native-bridge-") as tmp:
            d=pathlib.Path(tmp)
            gba=(ROOT/"src/gba.h").read_text()
            self.assertEqual(gba.count('target("arm"), '),1)
            (d/"gba_host.h").write_text(gba.replace('target("arm"), ',''))
            (d/"vrd32x_study_data.h").write_text("""
// Synthetic source only. No donor game bytes or generated source assets.
#define VRD_SAMPLE_SHIFT 4
#define VRD_TILE_SHIFT 4
#define VRD_TILE_AREA 256
#define VRD_HAS_NATIVE_OVERPASS 1
static const u16 vrd_native_bridge_bounds[4]={0,511,0,511};
static const u16 grid[4096] = {[0]=1};
static const u16 lower[256] = {[0 ... 255]=72};
static const u16* const vrd_study_lookups[5]={grid,grid,grid,grid,grid};
static const u16* const vrd_study_heights[5]={lower,lower,lower,lower,lower};
static const u16 upper_grid[4096] = {[0]=1,[1]=2};
static const u16 upper[512] = {[0 ... 255]=151,[256 ... 511]=161};
#define vrd_native_overpass_lookup upper_grid
#define vrd_native_overpass_heights upper
static const u8 blank[867]={0};
static const u8* const vrd_study_preview_bits[5]={blank,blank,blank,blank,blank};
static const u16 vrd_study_preview_bounds[5][4]={{0,511,0,511},{0,511,0,511},{0,511,0,511},{0,511,0,511},{0,511,0,511}};
""")
            (d/"verify.c").write_text("""
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
 vrd32x_study_set_height_reference(72*256);
 assert(vrd32x_study_height(3,96,96)==72*256);
 assert(vrd32x_study_ground_plane(3,96,96,8,&h,&gx,&gz));
 assert(h==72*256 && !gx && !gz);
 vrd32x_study_set_height_reference(151*256);
 assert(vrd32x_study_height(3,96,96)==151*256);
 assert(vrd32x_study_ground_plane(3,96,96,8,&h,&gx,&gz));
 assert(h==151*256 && !gx && !gz);
 assert(vrd32x_study_height(2,96,96)==72*256);
 vrd32x_study_set_height_reference(72*256);
 assert(vrd32x_study_height(3,288,96)==161*256);
 assert(vrd32x_study_surface(3,288,96)==SURF_ROAD);
 assert(vrd32x_study_surface(2,288,96)==SURF_GRASS);
 return 0;
}
""")
            command=cc+["-std=gnu99","-O2","-Wno-pointer-to-int-cast",
              "-ffunction-sections","-fdata-sections",
              "-fsanitize=shift,signed-integer-overflow","-fno-sanitize-recover=all",
              "-include",str(d/"gba_host.h"),"-DWITH_VRD_32X_TRACKS",
              "-I",str(d),"-I",str(ROOT/"src"),"-Wl,--gc-sections",
              str(d/"verify.c"),str(ROOT/"src/vrd32x_study.c"),"-o",str(d/"verify")]
            p=subprocess.run(command,capture_output=True,text=True,timeout=30)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            p=subprocess.run([str(d/"verify")],capture_output=True,text=True,timeout=15)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)

    def test_verified_private_32x_original_has_source_bridge_cycle(self):
        path=os.environ.get("VR32X_ROM")
        if not path:self.skipTest("Set VR32X_ROM for original cartridge test")
        from tools.decode_vrd_32x_surface import RoadSurface
        from tools.export_vrd_32x_surface_mesh import grid_samples
        from tools.vrd32x_layered_course import layered_main_cycle
        from tools.inspect_vrd_32x_tables import ROM_SHA256
        import numpy as np, hashlib
        source=pathlib.Path(path).read_bytes()
        self.assertEqual(hashlib.sha256(source).hexdigest(),ROM_SHA256)
        lo=RoadSurface(source,3,verify_identity=False)
        hi=RoadSurface(source,3,True,verify_identity=False)
        side,base=grid_samples(lo,16)
        _,bridge=grid_samples(hi,16)
        overlap=[(a,b) for a,b in zip(base,bridge)
                 if a is not None and b is not None]
        self.assertGreater(len(overlap),100)
        self.assertGreater(max(abs(a-b) for a,b in overlap),1000)
        m0=np.array([a is not None for a in base],dtype=bool).reshape((side,side))
        m1=np.array([a is not None for a in bridge],dtype=bool).reshape((side,side))
        pts,audit=layered_main_cycle(m0,m1,base,bridge,world_per_pixel=16)
        self.assertGreater(audit["gba_world_route_length_estimate"],20000)
        self.assertGreater(audit["source_bridge_points"],10)
        self.assertEqual(len(audit["bridge_transitions"]),2)
        self.assertEqual({n[0] for n in pts},{0,1})
        self.assertFalse(audit["original_start_finish_verified"])


if __name__=="__main__":
    unittest.main()
