"""Original-course format/terrain checks without copyrighted test fixtures.

Run normally with ``python3 -m unittest discover -s tests``.  Set VR_ROM to a
local supported original ROM to additionally validate every imported course.
The optional tests neither copy the ROM nor write extracted data to the repo.
"""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RUNTIME_SOURCE = ROOT / "src/vr_original.c"
sys.path.insert(0, str(ROOT / "tools"))

from vr_rom import (ASPHALT, ROM_SIZE, SOLID, RomError, parse_rom,
                    physics_height_at, surface_at)


def synthetic_course(planes=None, rows=65):
    """A deliberately artificial grid, unrelated to any game's map."""
    if planes is None:
        planes = [(0, 1, 0, 100 + i) for i in range(16)]
    return {"terrain": {
        "height_grid": [0] * (128 * rows), "height_rows": rows,
        "height_planes": planes,
        "surface_grid": [0] * (128 * rows), "surface_rows": rows,
        "surface_tiles": [0] * 64,
    }}


def run_host_runtime(fixture, harness):
    """Run production terrain/collision code using only artificial map data."""
    compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
    if not compiler or not shutil.which(compiler[0]):
        raise unittest.SkipTest("Host C compiler not available (set HOST_CC)")
    with tempfile.TemporaryDirectory(prefix="notracin-vr-terrain-") as directory:
        work = Path(directory)
        # Copy source only so its local include resolves to this synthetic header.
        source = work / "vr_original.c"
        source.write_text(RUNTIME_SOURCE.read_text())
        (work / "vr_original_mesh.h").write_text(fixture)
        main = work / "main.c"
        main.write_text(harness)
        binary = work / "terrain_check"
        command = compiler + ["-std=c99", "-O2", "-ffunction-sections", "-fdata-sections",
                              "-D__attribute__(x)=", "-I", str(ROOT / "src"),
                              "-Wl,--gc-sections", str(source), str(main),
                              str(ROOT / "src/render.c"), "-o", str(binary)]
        compiled = subprocess.run(command, capture_output=True, text=True, timeout=30)
        if compiled.returncode:
            raise AssertionError(compiled.stdout + compiled.stderr)
        executed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
        if executed.returncode:
            raise AssertionError(executed.stdout + executed.stderr)
        return executed.stdout


class RomRejectionTests(unittest.TestCase):
    def test_incomplete_and_headered_inputs_are_rejected(self):
        for length in (0, 0x200, ROM_SIZE - 1, ROM_SIZE + 512):
            with self.subTest(length=length), self.assertRaisesRegex(RomError, "Expected.*bytes"):
                parse_rom(bytes(length))

    def test_plausible_header_cannot_bypass_revision_hash(self):
        fake = bytearray(ROM_SIZE)
        fake[0x100:0x110] = b"SEGA MEGA DRIVE "
        fake[0x180:0x18C] = b"MK-1229 -00 "
        fake[0x18E:0x190] = bytes.fromhex("95ab")
        fake[0x1F0] = ord("U")
        with self.assertRaisesRegex(RomError, "Unsupported ROM revision"):
            parse_rom(bytes(fake))


class TerrainFormatTests(unittest.TestCase):
    def test_negative_division_truncates_toward_zero(self):
        course = synthetic_course([(-1, 3, 0, 100)] * 16)
        # -5 / 3 is -1 in 68000 DIVS, producing 100 - (-1)*16 = 116.
        # Python floor division would incorrectly produce 132.
        self.assertEqual(physics_height_at(course, 5, 0), 116)
        self.assertEqual(physics_height_at(course, 2, 0), 100)
        course["terrain"]["height_planes"] = [(5, -3, 0, 100)] * 16
        self.assertEqual(physics_height_at(course, 1, 0), 116)

    def test_signed_word_result_and_zero_denominator(self):
        course = synthetic_course([(-1, 1, 0, 32760)] * 16)
        self.assertEqual(physics_height_at(course, 1, 0), -32760)
        course["terrain"]["height_planes"] = [(100, 0, -100, 1234)] * 16
        self.assertEqual(physics_height_at(course, 17, -13), 0)

    def test_height_tile_and_four_by_four_subcell_order(self):
        course = synthetic_course()
        for row in range(4):
            for column in range(4):
                # Each physical 64-unit subcell chooses one full plane.
                self.assertEqual(physics_height_at(course, column * 64, -row * 64),
                                 100 + row * 4 + column)
        course["terrain"]["height_planes"] += [(0, 1, 0, 900 + i) for i in range(16)]
        course["terrain"]["height_grid"][64 * 128 + 65] = 1
        self.assertEqual(physics_height_at(course, 256, 0), 900)
        # A negative coordinate must enter the cell to the left, not round to 0.
        self.assertEqual(physics_height_at(course, -1, 0), 103)

    def test_surface_bits_are_most_significant_first(self):
        course = synthetic_course()
        course["terrain"]["surface_tiles"][0] = 0b00011011
        for index in range(4):
            self.assertEqual(surface_at(course, index * 16, 0), index)
        course["terrain"]["surface_tiles"][3] = 0b11100110
        self.assertEqual(surface_at(course, -16, 0), 2)
        # Increasing world Z moves up the packed map, with row wrap at zero.
        course["terrain"]["surface_tiles"][60] = 0b01000000
        course["terrain"]["surface_tiles"][4] = 0b11000000
        self.assertEqual(surface_at(course, 0, 16), 1)
        self.assertEqual(surface_at(course, 0, -16), 3)

    def test_partial_maps_and_outside_coordinates_fail_closed(self):
        course = synthetic_course()
        for x, z in ((16384, 0), (-16385, 0), (0, 16385), (0, -256),
                     (10**9, -10**9)):
            with self.subTest(x=x, z=z):
                self.assertEqual(physics_height_at(course, x, z), 0)
                self.assertEqual(surface_at(course, x, z), SOLID)
        self.assertEqual(surface_at(course, -16384, 0), ASPHALT)

    def test_gba_runtime_uses_the_same_units_and_signed_division(self):
        """Compile the production C queries against artificial ROM tables."""
        plane_values = ",".join("-1,3,2,100" for _ in range(16))
        fixture = f"""
#include "vr_original.h"
static const u16 height_grid[128 * 65] = {{0}};
static const s16 height_planes[64] = {{{plane_values}}};
static const u16 surface_grid[128 * 65] = {{0}};
static const u8 surface_tiles[64] = {{0x1b}};
static const VrOriginalCourse vr_original_courses[3] = {{
  {{0,0,0,height_grid,height_planes,65,surface_grid,surface_tiles,65}},
  {{0,0,0,height_grid,height_planes,65,surface_grid,surface_tiles,65}},
  {{0,0,0,height_grid,height_planes,65,surface_grid,surface_tiles,65}}
}};
"""
        harness = """
#include <assert.h>
#include <stdio.h>
#include "vr_original.h"
#include "world.h"
int main(void) {
    /* GBA X = meshX/4+8192 = physicsX/2+8192; Q8 height = physicsY*8. */
    assert(vr_original_height(0,8192,8192) == 800);
    assert(vr_original_height(0,8193,8192) == 800); /* DIVS -2/3 -> 0 */
    assert(vr_original_height(0,8194,8192) == 928); /* DIVS -4/3 -> -1 */
    assert(vr_original_height(0,8191,8192) == 3360); /* signed left cell */
    assert(vr_original_height(0,8192,8193) == -4448); /* negative Z local coord */
    assert(vr_original_height(0,8192,8064) == 0); /* beyond partial source grid */
    assert(vr_original_height(-1,8192,8192) == 0);
    assert(vr_original_height(3,8192,8192) == 0);
    assert(vr_original_surface(0,8192,8192) == SURF_ROAD);
    assert(vr_original_surface(0,8200,8192) != SURF_ROAD);
    assert(vr_original_surface(0,8208,8192) != SURF_ROAD);
    assert(vr_original_surface(0,8216,8192) != SURF_ROAD);
    assert(vr_original_surface(0,-1,8192) != SURF_ROAD);
    s32 h,gx,gz;
    assert(vr_original_ground_plane(0,8200,8180,2,&h,&gx,&gz) == 1);
    assert(gx == 85 && gz == 170);
    puts("PASS: production queries preserve original units, signed division and map bounds");
    return 0;
}
"""
        self.assertIn("PASS:", run_host_runtime(fixture, harness))


class OriginalCollisionTests(unittest.TestCase):
    """Circle contacts must find donor cells between perimeter sample rays."""

    @classmethod
    def setUpClass(cls):
        fixture = """
#include "vr_original.h"
static const u16 height_grid[128 * 65] = {0};
static const s16 height_planes[64] = {0,1,0,0};
extern u16 test_surface_grid[128 * 65];
extern u8 test_surface_tiles[64 * 16];
static const VrOriginalCourse vr_original_courses[3] = {
  {0,0,0,height_grid,height_planes,65,test_surface_grid,test_surface_tiles,65},
  {0,0,0,height_grid,height_planes,65,test_surface_grid,test_surface_tiles,65},
  {0,0,0,height_grid,height_planes,65,test_surface_grid,test_surface_tiles,65}
};
"""
        harness = """
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "vr_original.h"
#include "world.h"
u16 test_surface_grid[128 * 65];
u8 test_surface_tiles[64 * 16];
static u32 next_tile;

static void clear_map(void) {
    memset(test_surface_grid,0,sizeof(test_surface_grid));
    memset(test_surface_tiles,0,sizeof(test_surface_tiles));
    next_tile = 1; /* Tile zero is shared empty space. */
}

static void put_cell(s32 x, s32 z, u32 value) {
    /* Donor cells are 16 physics units = 8 GBA units, with reversed Z. */
    u32 gx = (u32)x >> 3, gz = (u32)(16384-z) >> 3;
    u32 grid_index = (gz >> 4) * 128 + (gx >> 4);
    assert(grid_index < 128*65 && value < 4);
    u32 tile = test_surface_grid[grid_index];
    if (!tile) {
        tile = next_tile++;
        assert(tile < 16);
        test_surface_grid[grid_index] = tile;
    }
    u32 offset = tile*64 + (gz&15)*4 + ((gx&15)>>2);
    u32 shift = (3-(gx&3))*2;
    test_surface_tiles[offset] = (test_surface_tiles[offset] & ~(3u<<shift)) | (value<<shift);
}

static void report(const char *name, s32 x, s32 z, s32 radius) {
    s32 nx=123, nz=456;
    s32 penetration = vr_original_collide(0,x,z,radius,&nx,&nz);
    printf("%s %d %d %d\\n",name,penetration,nx,nz);
}

int main(void) {
    clear_map();
    put_cell(8220,8204,2);
    /* A solid 8-unit cell entirely inside the radius48 circle. None of
       the former eight perimeter probes nor the center sees the cell. */
    static const s16 dx[8] = {16384,11585,0,-11585,-16384,-11585,0,11585};
    static const s16 dz[8] = {0,11585,16384,11585,0,-11585,-16384,-11585};
    assert(vr_original_surface(0,8192,8192) == SURF_ROAD);
    for (s32 k=0; k<8; k++)
        assert(vr_original_surface(0,8192+((dx[k]*48)>>14),8192+((dz[k]*48)>>14)) == SURF_ROAD);
    report("interior_cell",8192,8192,48);

    clear_map();
    put_cell(8204,8188,2); /* x=[8200,8208), z=(8184,8192]. */
    report("node_side",8199,8188,2);
    report("node_corner",8199,8193,2);
    report("side_tangent",8198,8188,2);
    report("corner_clear",8199,8194,2);
    report("inside_cell",8204,8188,2);

    clear_map();
    put_cell(8192,8192,1);
    put_cell(8200,8192,3);
    report("non_solid_surfaces",8192,8192,16);

    clear_map();
    report("cross_left",1,8192,2);
    report("cross_right",16383,8192,2);
    report("cross_partial_bottom",8192,8065,2);
    report("cross_top",8192,16383,2);
    report("outside_left",-2,8192,2);
    report("outside_right",16385,8192,2);
    report("outside_partial_bottom",8192,8063,2);
    report("outside_top",8192,16385,2);
    report("zero_radius",-2,8192,0);
    report("negative_radius",8192,8192,-1);
    return 0;
}
"""
        cls.contacts = {}
        for line in run_host_runtime(fixture, harness).splitlines():
            name, penetration, nx, nz = line.split()
            cls.contacts[name] = tuple(map(int, (penetration, nx, nz)))

    def assert_contact(self, name, x_sign=None, z_sign=None):
        penetration, nx, nz = self.contacts[name]
        self.assertGreater(penetration, 0, name)
        self.assertNotEqual((nx,nz), (0,0), name)
        for component, sign in ((nx,x_sign), (nz,z_sign)):
            if sign is not None:
                if sign:
                    self.assertGreater(component*sign, 0, name)
                else:
                    self.assertEqual(component, 0, name)

    def test_small_solid_cell_inside_broad_phase_circle_is_found(self):
        self.assert_contact("interior_cell", -1, -1)

    def test_body_node_edges_and_corners(self):
        self.assert_contact("node_side", -1, 0)
        self.assert_contact("node_corner", -1, 1)
        for name in ("side_tangent", "corner_clear"):
            with self.subTest(name=name):
                self.assertEqual(self.contacts[name][0], 0, name)

    def test_spawn_inside_solid_cell_has_a_recovery_direction(self):
        self.assert_contact("inside_cell")

    def test_other_surface_codes_do_not_create_walls(self):
        self.assertEqual(self.contacts["non_solid_surfaces"], (0,0,0))

    def test_circle_intersecting_map_edge_collides_inward(self):
        for name, xs, zs in (("cross_left",1,0), ("cross_right",-1,0),
                             ("cross_partial_bottom",0,1), ("cross_top",0,-1)):
            with self.subTest(name=name):
                self.assert_contact(name,xs,zs)

    def test_outside_map_positions_recover_inward(self):
        for name, xs, zs in (("outside_left",1,0), ("outside_right",-1,0),
                             ("outside_partial_bottom",0,1), ("outside_top",0,-1)):
            with self.subTest(name=name):
                self.assert_contact(name,xs,zs)

    def test_nonpositive_radius_is_an_empty_query(self):
        for name in ("zero_radius", "negative_radius"):
            with self.subTest(name=name):
                self.assertEqual(self.contacts[name], (0,0,0))


@unittest.skipUnless(os.environ.get("VR_ROM"), "Set VR_ROM for tests using your own original ROM")
class OriginalRomTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = Path(os.environ["VR_ROM"]).expanduser().read_bytes()
        cls.imported = parse_rom(cls.data)

    def test_all_original_meshes_are_exactly_bounded(self):
        expected = ((0x138000,128,3264,0x14BDD0),
                    (0x14C000,128,3657,0x16254E),
                    (0x164000,166,4420,0x17EA5C))
        for course, (table, sections, polygons, end) in zip(self.imported["courses"], expected):
            self.assertEqual(course["mesh_offset"], table)
            self.assertEqual(len(course["sections"]), sections)
            self.assertEqual(sum(len(s["polygons"]) for s in course["sections"]), polygons)
            self.assertEqual(course["sections"][-1]["end"], end)
            self.assertEqual(course["sections"][0]["offset"], table + sections * 4)

    def test_routes_close_with_every_native_progress_step_on_asphalt(self):
        for course, length in zip(self.imported["courses"], (125,165,166)):
            route = course["route"]
            self.assertEqual(len(route), length)
            self.assertEqual([n["progress"] for n in route], list(range(length)))
            self.assertEqual(route[-1]["neighbors"][1], route[0]["index"])
            for node in route:
                self.assertEqual(surface_at(course,node["physics_x"],node["physics_z"]), ASPHALT)
                self.assertEqual(node["x"],node["physics_x"]*2)
                self.assertEqual(node["z"],node["physics_z"]*2)
                self.assertEqual(node["y"]*8,node["physics_y"])

    def test_known_native_cpu_query_results(self):
        # These scalar observations were independently reproduced by executing
        # the original 68000 helpers, not taken from the port's C queries.
        observations = (
            ((-3696,-3600,2555,0),(-3349,694,2564,3)),
            ((-3286,48,7660,0),(-3320,8633,7647,0)),
            ((-5112,-3048,947,0),(-4506,3783,786,0)),
        )
        for course, samples in zip(self.imported["courses"], observations):
            for x,z,height,surface in samples:
                self.assertEqual(physics_height_at(course,x,z),height)
                self.assertEqual(surface_at(course,x,z),surface)

    def test_single_byte_mutation_is_rejected_before_import(self):
        modified = bytearray(self.data)
        modified[0x138200] ^= 1
        with self.assertRaisesRegex(RomError, "Unsupported ROM revision"):
            parse_rom(bytes(modified))


if __name__ == "__main__":
    unittest.main()
