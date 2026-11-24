"""Source-only checks for the 16x16 source-BSP road collision option.

All masks/heights here are synthetic and do NOT contain Sega game assets.
Test the real production C height/surface/ground plane on a 16-sample tile,
plus the correct independent eight-course menu compile-time indices.
"""
from pathlib import Path
import os, shlex, shutil, subprocess, tempfile, unittest

ROOT=Path(__file__).resolve().parents[1]


class Original32xDensityTests(unittest.TestCase):
    def test_32x_only_game_layout_and_double_density_surface(self):
        cc=shlex.split(os.environ.get("HOST_CC","cc"))
        if not cc or not shutil.which(cc[0]):
            self.skipTest("Host compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="ntr-32x-density-") as directory:
            d=Path(directory)
            header=(ROOT/"src/gba.h").read_text()
            self.assertEqual(header.count('target("arm"), '),1)
            (d/"gba_host.h").write_text(header.replace('target("arm"), ',''))
            samples=[10+3*x+4*z for z in range(16) for x in range(16)]
            (d/"vrd32x_study_data.h").write_text(
                '// SYNTHETIC test, NEVER original donor content.\n'
                '#define VRD_SAMPLE_SHIFT 4\n'
                '#define VRD_TILE_SHIFT 4\n'
                '#define VRD_TILE_AREA 256\n'
                'static const u16 cell[4096] = {[0]=1};\n'
                'static const u16 h[256] = {'+','.join(map(str,samples))+'};\n'
                'static const u16 *const vrd_study_lookups[5]={cell,cell,cell,cell,cell};\n'
                'static const u16 *const vrd_study_heights[5]={h,h,h,h,h};\n'
                'static const u16 vrd_study_preview_bounds[5][4]={{0,255,0,255},{0,255,0,255},{0,255,0,255},{0,255,0,255},{0,255,0,255}};\n'
                'static const u8 bitmap[867]={0};\n'
                'static const u8 *const vrd_study_preview_bits[5]={bitmap,bitmap,bitmap,bitmap,bitmap};\n')
            (d/"check.c").write_text(r"""
#include <assert.h>
#include "track.h"
#include "world.h"
#include "vrd32x_study.h"
const TrackDef g_tracks[TRACK_COUNT]={0};
const TrackDef *g_track=0;
int main(void) {
  s32 h=0,gx=0,gz=0;
  assert(TRACK_SET_COUNT==2 && TRACK_COUNT==8 && TRACK_VRD_FIRST==3);
  assert(TRACK_SET_VRD==1 && track_set_count(TRACK_SET_VRD)==5);
  g_track=g_tracks+TRACK_VRD_FIRST+4;
  assert(vrd32x_study_slot()==4);
  assert(vrd32x_study_height(0,0,0)==10*256);
  assert(vrd32x_study_height(0,16,0)==13*256);
  assert(vrd32x_study_height(0,0,16)==14*256);
  assert(vrd32x_study_height(0,112,96)==55*256);
  assert(vrd32x_study_height(0,120,96)==55*256+3*128);
  assert(vrd32x_study_height(0,120,104)==55*256+7*128);
  assert(vrd32x_study_surface(0,255,64)==SURF_ROAD);
  assert(vrd32x_study_surface(0,256,64)==SURF_GRASS);
  assert(vrd32x_study_ground_plane(0,96,96,8,&h,&gx,&gz));
  assert(h==52*256 && gx==48 && gz==64);
  assert(!vrd32x_study_ground_plane(0,240,96,8,&h,&gx,&gz));
  assert(h==0 && gx==0 && gz==0);
  assert(vrd32x_study_height(0,-1,64)==0);
  assert(vrd32x_study_surface(5,100,100)==SURF_GRASS);
  return 0;
}
""")
            args=cc+["-std=c99","-O2","-Wno-pointer-to-int-cast",
                     "-ffunction-sections","-fdata-sections",
                     "-fsanitize=shift,signed-integer-overflow",
                     "-fno-sanitize-recover=all","-include",str(d/"gba_host.h"),
                     "-DWITH_VRD_32X_TRACKS","-I",str(d),"-I",str(ROOT/"src"),
                     "-Wl,--gc-sections",str(d/"check.c"),
                     str(ROOT/"src/vrd32x_study.c"),"-o",str(d/"check")]
            build=subprocess.run(args,capture_output=True,text=True,timeout=45)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(d/"check")],capture_output=True,text=True,timeout=15)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_legacy_source_grid_and_verification_gate_remain(self):
        code=(ROOT/"tools/import_vrd_32x_explorer.py").read_text()
        self.assertIn("choices=(8,16)",code)
        self.assertIn("default=8",code)
        self.assertIn("ROM_SHA256",code)
        self.assertIn("inspect_image(source,require_identity=True)",code)
        self.assertIn("native_collision_mesh_in_gba=exact_mesh",code)
        # Source-only CI has no commercial 32X data and may not install the
        # private importer dependencies. Still ensure the entire optional
        # import program is syntactically executable Python.
        compile(code,str(ROOT/"tools/import_vrd_32x_explorer.py"),"exec")


if __name__=="__main__":
    unittest.main()
