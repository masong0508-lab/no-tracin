// Exercise race.c's actual pose and draw dispatch, then capture the matrix
// car.c submits for both its detailed and distant rival models.
#include <assert.h>
#include <stdio.h>
#include "race.c"

static s32 drawn[9], draw_x, draw_y, draw_z, draw_calls, view_depth;

#ifndef TEST_ORIGINAL_ROM
const TrackDef g_tracks[TRACK_COUNT] = {{0}};
const TrackDef *g_track;
s16 track_pitch[256];
static s32 slope_x, slope_z, pose_heading, queries;

void track_point(s32 d, s32 lat, s32 *x, s32 *z, s32 *y, s32 *heading, s32 *hint)
{
    (void)d;
    *x = 8192 + ((icos(pose_heading >> 6) * lat) >> 14);
    *z = 8192 - ((isin(pose_heading >> 6) * lat) >> 14);
    *y = 64 * 256 + 72 + slope_x * (*x - 8192) + slope_z * (*z - 8192);
    *heading = pose_heading;
    *hint = 7;
}

s32 track_height(s32 x, s32 z)
{
    queries++;
    return 64 * 256 + 72 + slope_x * (x - 8192) + slope_z * (z - 8192);
}
#endif

static void capture(s32 x, s32 y, s32 z, const s32 *matrix)
{
    assert(x == rivals[0].x && y == (rivals[0].y >> 8) && z == rivals[0].z);
    if (draw_calls)
        for (s32 i = 0; i < 9; i++) assert(drawn[i] == matrix[i]);
    for (s32 i = 0; i < 9; i++) drawn[i] = matrix[i];
    draw_x = x; draw_y = y; draw_z = z;
    draw_calls++;
}

void __wrap_r_mesh(s32 x, s32 y, s32 z, const s32 *matrix, const Mesh *mesh)
{
    assert(mesh && mesh->vert_count);
    capture(x, y, z, matrix);
}

void __wrap_r_box_mat(s32 x, s32 y, s32 z, const s32 *matrix,
                      s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1, s32 material)
{
    (void)x0; (void)y0; (void)z0; (void)x1; (void)y1; (void)z1; (void)material;
    capture(x, y, z, matrix);
}

s32 __wrap_r_visible(s32 x, s32 z, s32 radius)
{
    (void)radius;
    return x == rivals[0].x && z == rivals[0].z;
}

s32 __wrap_r_depth(s32 x, s32 z, s32 *side)
{
    (void)x; (void)z;
    *side = 0;
    return view_depth;
}

static void draw_pose(s32 depth)
{
    g_race.mode = RACE_ARCADE;
    r_far = 2000;
    for (s32 i = 1; i < RIVALS; i++) rivals[i].x = rivals[i].z = -1;
    draw_calls = 0;
    view_depth = depth;
    race_draw_rivals();
    assert(draw_calls == (depth < 200 ? 5 : depth < 320 ? 1 : depth < 600 ? 2 : 1));
}

static void verify_road_plane(s32 gx, s32 gz)
{
    // All four contact-plane corners must remain on the road. This checks
    // the rotation order, banking sign and heading without reproducing the
    // pitch/roll calculation being tested. Quantized trig permits one unit.
    for (s32 x = -20; x <= 20; x += 40)
        for (s32 z = -44; z <= 44; z += 88) {
            s32 dx = drawn[0] * x + drawn[6] * z;
            s32 dy = drawn[1] * x + drawn[7] * z;
            s32 dz = drawn[2] * x + drawn[8] * z;
            long long error = (long long)dy * 256 - (long long)gx * dx - (long long)gz * dz;
            if (error < 0) error = -error;
            assert(error <= 256 * 16384);
        }
}

int main(void)
{
    r_init(); // production sine/atan tables; this initialization uses only RAM
#ifdef TEST_ORIGINAL_ROM
    // Reproduce the independently observed Acropolis bank using the user's
    // local import. No route tables or other ROM assets live in this test.
    track_load(TRACKS_PER_SET + 2);
    const TrackPt *point = &g_track->pts[103];
    for (s32 lat = -22; lat <= 22; lat += 22) {
        Rival *r = &rivals[0];
        r->s = point->dist * 256; r->lat = lat; r->hint = 103;
        rival_pose(r);
        // Curved banks can lift the mean wheel height a little above their
        // centre, just as the physical car's suspension does.
        assert(iabs(r->y - track_height(r->x, r->z)) < 512);
        assert(r->roll < -40 && r->roll > -90);
        for (s32 depth = 100; depth <= 700; depth += 300) {
            draw_pose(depth);
            // Check the actual donor heights beneath the rendered base,
            // allowing for local road curvature, integer model position and
            // the original half-unit height steps. All sills are 5 units up.
            for (s32 x = -20; x <= 20; x += 40)
                for (s32 z = -44; z <= 44; z += 88) {
                    s32 wx = draw_x + ((drawn[0] * x + drawn[6] * z) >> 14);
                    s32 wy = draw_y * 256 + ((drawn[1] * x + drawn[7] * z) >> 6);
                    s32 wz = draw_z + ((drawn[2] * x + drawn[8] * z) >> 14);
                    s32 floor = track_height(wx, wz);
                    if (iabs(wy - floor) >= 896)
                        fprintf(stderr, "bank lat=%d heading=%d pose=%d,%d corner=%d,%d world=%d,%d rendered=%d floor=%d\n",
                                lat, r->heading, r->pitch, r->roll, x, z, wx, wz, wy, floor);
                    assert(iabs(wy - floor) < 896);
                }
        }
        printf("Acropolis bank lateral %d: pitch=%d roll=%d height=%d/256\n",
               lat, r->pitch, r->roll, r->y);
    }
    puts("PASS: native Acropolis bank heights and both rival render models agree");
#else
    static const s32 headings[] = {0, 16384, 32768, 49152, 42976};
    static const s32 slopes[][2] = {{0,0}, {0,96}, {112,0}, {-96,80}, {512,512}, {-512,-512}};
    track_pitch[7] = 23;
    for (s32 set = 0; set < TRACK_SET_COUNT; set++) {
        g_track = &g_tracks[set * TRACKS_PER_SET];
        for (u32 h = 0; h < sizeof(headings) / sizeof(headings[0]); h++)
            for (u32 s = 0; s < sizeof(slopes) / sizeof(slopes[0]); s++) {
                pose_heading = headings[h]; slope_x = slopes[s][0]; slope_z = slopes[s][1];
                Rival *r = &rivals[0];
                r->lat = h & 1 ? 13 : -13;
                queries = 0;
                rival_pose(r);
                if (set == TRACK_SET_CUSTOM) {
                    assert(queries == 0 && r->pitch == 23 && r->roll == 0);
                } else {
                    assert(queries == 4);
                    assert(r->y == 64 * 256 + 72 + slope_x * (r->x - 8192) + slope_z * (r->z - 8192));
                }
                for (s32 depth = 100; depth <= 700; depth += 300) {
                    draw_pose(depth);
                    if (set == TRACK_SET_CUSTOM) assert(drawn[1] == 0 && drawn[7] == isin(23));
                    else verify_road_plane(slope_x, slope_z);
                }
            }
    }
#ifdef WITH_VR_ORIGINAL_TRACKS
    // A discontinuity beside the road is not a usable plane for car banking.
    slope_x = 513; slope_z = 0;
    rival_pose(&rivals[0]);
    assert(rivals[0].pitch == 23 && rivals[0].roll == 0);
#endif
    puts("PASS: custom rival poses stay unchanged; original models follow their road plane");
#endif
    return 0;
}
