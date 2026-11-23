"""Source-only terrain tests for the private 32X road-study renderer.

The fake 8x8 tile is explicitly synthetic: no Sega ROM-derived data is
shipped to public CI. Test production C height, surface and normal queries.
"""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RoadStudyTerrainTests(unittest.TestCase):
    def test_height_surface_normal_bounds_and_study_slot(self):
        cc = shlex.split(os.environ.get('HOST_CC', 'cc'))
        if not cc or not shutil.which(cc[0]):
            self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory(prefix='notracin-vrd-study-') as directory:
            temp = Path(directory)
            header = temp / 'gba_host.h'
            header.write_text((ROOT/'src/gba.h').read_text().replace('target("arm"), ', ''))
            heights = [10 + 4 * row + 3 * col for row in range(8) for col in range(8)]
            tile = temp / 'vrd32x_study_data.h'
            tile.write_text('''// SYNTHETIC ONLY. Sampled genuine source data is never committed.
static const u16 test_tile[4096] = { [0] = 1 };
static const u16 test_h[64] = { ''' + ', '.join(map(str, heights)) + ''' };
static const u16 test_h2[64] = { ''' + ', '.join(map(str, [90 - 4 * row - 3 * col for row in range(8) for col in range(8)])) + ''' };
static const u16 test_h3[64] = { ''' + ', '.join(map(str, [65535 if row < 4 and col < 4 else 10 + 4 * row + 3 * col for row in range(8) for col in range(8)])) + ''' };
static const u16 *const vrd_study_lookups[5] = { test_tile, test_tile, test_tile, test_tile, test_tile };
static const u16 *const vrd_study_heights[5] = { test_h, test_h2, test_h3, test_h, test_h };
''')
            fixture = temp / 'verify.c'
            fixture.write_text(r'''
#include <assert.h>
#include "track.h"
#include "world.h"
#include "vrd32x_study.h"
const TrackDef g_tracks[TRACK_COUNT] = {0};
const TrackDef *g_track = 0;
int main(void) {
    s32 h=1,gx=1,gz=1;
    assert(vrd32x_study_slot() == -1);
    g_track = g_tracks + TRACK_VRD_FIRST;
    assert(vrd32x_study_slot() == 0);
    g_track = g_tracks + TRACK_VRD_FIRST + 4;
    assert(vrd32x_study_slot() == 4);
    assert(vrd32x_study_height(0,96,96) == 31*256);
    // Bilinear Q8 road height, including half-grid positions. The mask
    // remains discrete: no blended bridge into an unsupported neighboring cell.
    assert(vrd32x_study_height(0,112,96) == (31*256 + 3*128));
    assert(vrd32x_study_height(0,96,112) == (31*256 + 4*128));
    assert(vrd32x_study_height(0,112,112) == (31*256 + 7*128));
    assert(vrd32x_study_height(0,239,96) == 43*256);
    assert(vrd32x_study_height(0,256,96) == 0);
    assert(vrd32x_study_surface(0,255,96) == SURF_ROAD);
    assert(vrd32x_study_surface(0,256,96) == SURF_GRASS);
    // Negative grades are safe too; no unsigned wrap or truncated Q8.
    assert(vrd32x_study_height(1,112,112) == (90*256 - 49*128));
    assert(vrd32x_study_surface(0,96,96) == SURF_ROAD);
    assert(vrd32x_study_ground_plane(0,96,96,8,&h,&gx,&gz));
    assert(h == 31*256 && gx == 24 && gz == 32);
    assert(vrd32x_study_ground_plane(0,112,112,8,&h,&gx,&gz));
    assert(h == 31*256 + 7*128 && gx == 24 && gz == 32);
    assert(vrd32x_study_ground_plane(1,112,112,8,&h,&gx,&gz));
    assert(h == 90*256 - 49*128 && gx == -24 && gz == -32);
    assert(!vrd32x_study_ground_plane(0,239,96,8,&h,&gx,&gz));
    assert(h == 0 && gx == 0 && gz == 0);
    assert(vrd32x_study_surface(0,260,96) == SURF_GRASS);
    assert(vrd32x_study_height(0,-32,96) == 0);
    assert(vrd32x_study_surface(0,16384,96) == SURF_GRASS);
    h=gx=gz=12345;
    assert(!vrd32x_study_ground_plane(0,10,10,24,&h,&gx,&gz));
    assert(h==0 && gx==0 && gz==0);
    assert(vrd32x_study_surface(5,96,96) == SURF_GRASS);
    // The old tile-level map preview wrongly painted all four quadrants,
    // including the synthetic unsupported top-left 4x4 source mask.
    static volatile u16 map_page[SCREEN_W * SCREEN_H / 2];
    vrd32x_study_map(map_page,2,10,10,128,128,5);
#define MAP_PIXEL(xx,yy) (((xx)&1) ? ((map_page[((yy)*SCREEN_W+(xx))/2]>>8)&255) : (map_page[((yy)*SCREEN_W+(xx))/2]&255))
    assert(MAP_PIXEL(10,10)==0);
    assert(MAP_PIXEL(11,10)==5);
    assert(MAP_PIXEL(10,11)==5);
    assert(MAP_PIXEL(11,11)==5);
    vrd32x_study_map(map_page,5,10,10,128,128,5); // invalid slot must be safe
    assert(MAP_PIXEL(10,10)==0);
    return 0;
}
''')
            cmd = cc + [
                '-std=c99', '-O2', '-ffunction-sections', '-fdata-sections',
                '-Wno-pointer-to-int-cast', '-fsanitize=shift,signed-integer-overflow',
                '-fno-sanitize-recover=all', '-include', str(header),
                '-DWITH_VR_ORIGINAL_TRACKS', '-DWITH_VRD_32X_TRACKS',
                '-I', str(temp), '-I', str(ROOT/'src'), '-Wl,--gc-sections',
                str(fixture), str(ROOT/'src/vrd32x_study.c'), '-o', str(temp/'verify'),
            ]
            p = subprocess.run(cmd, text=True, capture_output=True, timeout=30)
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
            p = subprocess.run([str(temp/'verify')], text=True, capture_output=True, timeout=15)
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)

    def test_optional_import_has_strict_version_gate(self):
        # Exact verified source identity is required before decoding.
        code = (ROOT/'tools/import_vrd_32x_explorer.py').read_text()
        self.assertIn("hashlib.sha256(source).hexdigest()!=ROM_SHA256", code)
        self.assertIn("'skeleton_cycle_NOT_authentic_start_finish_or_lap'", code)
        self.assertIn('lap_start_finish_verified=False', code)


if __name__ == '__main__':
    unittest.main()
