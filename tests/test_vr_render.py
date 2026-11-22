"""Exercise imported mesh identity and frame submission with synthetic data."""

import copy
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from import_vr_original import mesh_header


def synthetic_courses():
    face = {"flags": 0x40, "colors": [1, 1],
            "vertices": [[-64, 0, -64], [64, 0, -64],
                         [64, 0, 64], [-64, 0, 64]]}
    shifted = copy.deepcopy(face)
    for vertex in shifted["vertices"]:
        vertex[0] += 256
    colour = copy.deepcopy(face)
    colour["colors"] = [2, 2]
    flags = copy.deepcopy(face)
    flags["flags"] |= 0x20
    subpixel = copy.deepcopy(face)
    subpixel["vertices"][0][0] += 1
    # The last variant rounds to identical GBA coordinates. It remains a
    # distinct source face; equality is not guessed from reduced precision.
    course = {
        "name": "SYNTHETIC", "polygon_count": 6,
        "mesh_offset": 0, "descriptor_offset": 0,
        "palette_rgb3": [(0, 0, 0), (7, 7, 7), (7, 0, 0)] + [(0, 0, 0)] * 13,
        "sections": [{"polygons": [face, shifted]},
                     {"polygons": [copy.deepcopy(face), colour, flags, subpixel]}],
        "terrain": {"height_grid": [0] * 128, "height_rows": 1,
                    "height_planes": [(0, 1, 0, 0)] * 16,
                    "surface_grid": [0] * 128, "surface_rows": 1,
                    "surface_tiles": [0] * 64},
    }
    return [copy.deepcopy(course) for _ in range(3)]


class OriginalMeshSubmissionTests(unittest.TestCase):
    def test_repeated_source_faces_draw_once_and_reset_each_frame(self):
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Host C compiler not available (set HOST_CC)")
        header, stats = mesh_header(synthetic_courses())
        for course in stats:
            self.assertEqual(course["rendered_polygons"], 6)
            self.assertEqual(course["unique_polygons"], 5)
            self.assertEqual(course["duplicate_polygons"], 1)
        harness = r'''
#include <assert.h>
#include <stdio.h>
#include "vr_original.h"
#include "vr_original_mesh.h"
static s32 calls, capacity=320;
s32 r_visible(s32 x,s32 z,s32 radius) { (void)x;(void)z;(void)radius;return 1; }
s32 r_depth(s32 x,s32 z,s32 *side) { (void)x;*side=0;return z; }
s32 r_faces_free(void) { return capacity-calls; }
void r_face(const Vec3 *v,s32 n,u8 color,u32 flags) {
    (void)v;(void)color;(void)flags;assert(n==4);calls++;
}
int main(void) {
    const VrOriginalCourse *c=&vr_original_courses[0];
    assert(c->section_count==2);
    assert(c->sections[0].first==0 && c->sections[0].count==2);
    assert(c->sections[1].first==2 && c->sections[1].count==4);
    assert(c->faces[0].identity==c->faces[2].identity);
    for(s32 i=3;i<6;i++) assert(c->faces[0].identity!=c->faces[i].identity);
    for(s32 i=0;i<12;i++) assert(c->faces[0].xyz[i]==c->faces[5].xyz[i]);
    vr_original_draw(0,0,0,0);assert(calls==5);
    vr_original_draw(0,0,0,1);assert(calls==10);
    calls=0;capacity=33;
    vr_original_draw(0,0,0,2);assert(calls==1); /* preserve 32 player/effect faces */
    calls=0;capacity=320;
    vr_original_draw(0,0,0,3);assert(calls==5);
    vr_original_draw(-1,0,0,4);assert(calls==5);
    puts("PASS: source identities, frame reset and reserved queue capacity");
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="notracin-vr-render-") as directory:
            work = Path(directory)
            gba = (ROOT / "src/gba.h").read_text()
            self.assertEqual(gba.count('target("arm"), '), 1)
            (work / "gba_host.h").write_text(gba.replace('target("arm"), ', ""))
            (work / "vr_original_mesh.h").write_text(header)
            (work / "vr_original.c").write_text((ROOT / "src/vr_original.c").read_text())
            (work / "main.c").write_text(harness)
            binary = work / "mesh_check"
            command = compiler + ["-std=c99", "-O2", "-ffunction-sections", "-fdata-sections",
                                  "-include", str(work / "gba_host.h"), "-I", str(ROOT / "src"),
                                  "-Wl,--gc-sections", str(work / "vr_original.c"),
                                  str(work / "main.c"), "-o", str(binary)]
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PASS:", result.stdout)


if __name__ == "__main__":
    unittest.main()
