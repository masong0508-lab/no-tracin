// External horizontal impulses must reach the real Verlet state without
// changing the body's shape, rotation, free parts, or another car.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "softbody.c"

s32 car_g = 35, car_grip = 38, car_power = 28000, car_crashes = 1;
s32 cheat_fly, cheat_hop, cheat_glitch;
const TrackDef *g_track;
const TrackDef g_tracks[TRACK_COUNT] = {0};
s32 world_height(s32 x, s32 z) { (void)x; (void)z; return 0; }
s32 world_surface(s32 x, s32 z) { (void)x; (void)z; return SURF_ROAD; }
s32 world_in_water(s32 x, s32 z) { (void)x; (void)z; return 0; }
s32 world_collide(s32 x, s32 z, s32 r, s32 *nx, s32 *nz)
{
    (void)x; (void)z; (void)r; *nx = *nz = 0; return 0;
}
s32 world_ground_plane(s32 x, s32 z, s32 r, s32 *h, s32 *gx, s32 *gz)
{
    (void)x; (void)z; (void)r; *h = *gx = *gz = 0; return 1;
}
s32 track_find(s32 x, s32 z, TrackHit *hit)
{
    (void)x; (void)z; *hit = (TrackHit){0}; return 1;
}
// Artificial track fixture: route-only lookup shares its known geometry.
s32 track_find_road(s32 x, s32 z, TrackHit *hit) { return track_find(x, z, hit); }
s32 track_height_near(s32 segment, s32 x, s32 z)
{
    (void)segment; (void)x; (void)z; return 0;
}
void track_gradient(s32 x, s32 z, s32 *gx, s32 *gz)
{
    (void)x; (void)z; *gx = *gz = 0;
}
void car_respawn(Car *car) { (void)car; assert(0 && "Flat-road fixture cannot respawn"); }
void sound_play(s32 sound) { (void)sound; }
void game_message(const char *a, const char *b, s32 palette, s32 steps)
{
    (void)a; (void)b; (void)palette; (void)steps;
}

static Car reset_case(void)
{
    step_n = safe_timer = hit_pending = ebox_ok = 0;
    return (Car){.x = 1000 * 256, .z = 6000 * 256, .vz = 2000, .mode = CAR_GROUND};
}

static void shape_and_detached_parts(void)
{
    Car car = reset_case();
    sb_reset(&car);
    // A dented frame with angular motion, a detached wing, and a loose hub.
    nd[0].x += 128;
    nd[0].px += 128;
    for (s32 i = 0; i < NN; i++) {
        nd[i].px += rest[i][2];
        nd[i].pz -= rest[i][0];
    }
    wing_off = 1;
    hub_on[2] = 0;
    Node before[NN];
    memcpy(before, nd, sizeof(nd));
    Car expected = car;
    expected.vx += 32;
    expected.vz -= 19;
    sb_add_velocity(&car, 32, -19);
    assert(memcmp(&car, &expected, sizeof(car)) == 0);
    for (s32 i = 0; i < NN; i++) {
        if (i == 16 || i == 17 || i == 20) {
            assert(memcmp(&nd[i], &before[i], sizeof(Node)) == 0);
        } else {
            assert(nd[i].x == before[i].x && nd[i].y == before[i].y && nd[i].z == before[i].z);
            assert(nd[i].px == before[i].px - 32 && nd[i].py == before[i].py && nd[i].pz == before[i].pz + 19);
        }
        if (i < 16) {
            assert((nd[i].x - nd[i].px) - (nd[0].x - nd[0].px) ==
                   (before[i].x - before[i].px) - (before[0].x - before[0].px));
            assert((nd[i].z - nd[i].pz) - (nd[0].z - nd[0].pz) ==
                   (before[i].z - before[i].pz) - (before[0].z - before[0].pz));
        }
    }
}

static void ownership(void)
{
    Car car = reset_case(), other = reset_case();
    sb_reset(&car);
    Node before[NN];
    memcpy(before, nd, sizeof(nd));
    Car original = car, other_original = other;
    sb_add_velocity(&other, 32, -19);
    assert(memcmp(&car, &original, sizeof(car)) == 0);
    assert(memcmp(&other, &other_original, sizeof(other)) == 0);
    assert(memcmp(nd, before, sizeof(nd)) == 0);
    sb_invalidate();
    sb_add_velocity(&car, 32, -19);
    sb_add_velocity(0, 32, -19);
    assert(memcmp(&car, &original, sizeof(car)) == 0);
    assert(memcmp(nd, before, sizeof(nd)) == 0);
}

static Car drive(s32 added)
{
    Car car = reset_case();
    sb_reset(&car);
    sb_step(&car, KEY_A);
    if (added) sb_add_velocity(&car, 0, 32);
    sb_step(&car, KEY_A);
    return car;
}

int main(void)
{
    r_init();
    memset(g_tune, 10, sizeof(g_tune));
    tune_apply();
    shape_and_detached_parts();
    ownership();
    Car ordinary = drive(0), pushed = drive(1);
    assert(pushed.z > ordinary.z && pushed.vz > ordinary.vz);
    assert(pushed.x == ordinary.x && pushed.y == ordinary.y);
    printf("PASS: attached-body impulse persists through sb_step (%d > %d), preserving shape, rotation, free parts and ownership\n",
           pushed.vz, ordinary.vz);
    return 0;
}
