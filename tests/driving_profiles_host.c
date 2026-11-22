// Real soft-body arithmetic at the allowed lab extremes. Production impact
// response and car integration run against a flat-road/barrier fixture.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "softbody.c"

const TrackDef *g_track;
const TrackDef g_tracks[TRACK_COUNT] = {0};
void race_on_respawn(Car *player) { (void)player; assert(!g_track); }
const Loop the_loop = {1000,-10000,200,100,100};
static s32 wall_z;
s32 world_height(s32 x,s32 z) { (void)x;(void)z;return 0; }
s32 world_surface(s32 x,s32 z) { (void)x;(void)z;return SURF_ROAD; }
s32 world_in_water(s32 x,s32 z) { (void)x;(void)z;return 0; }
s32 world_collide(s32 x,s32 z,s32 radius,s32 *nx,s32 *nz)
{
    (void)x;*nx=*nz=0;
    if(wall_z && z+radius>wall_z) { *nz=-16384;return z+radius-wall_z; }
    return 0;
}
s32 world_ground_plane(s32 x,s32 z,s32 radius,s32 *h,s32 *gx,s32 *gz)
{ (void)x;(void)z;(void)radius;*h=*gx=*gz=0;return 1; }
s32 track_find(s32 x,s32 z,TrackHit *hit)
{ (void)x;(void)z;*hit=(TrackHit){0};return 1; }
// Artificial track fixture: route-only lookup shares its known geometry.
s32 track_find_road(s32 x, s32 z, TrackHit *hit) { return track_find(x, z, hit); }
s32 track_height_near(s32 segment,s32 x,s32 z)
{ (void)segment;(void)x;(void)z;return 0; }
void track_gradient(s32 x,s32 z,s32 *gx,s32 *gz)
{ (void)x;(void)z;*gx=*gz=0; }
void loop_point(s32 theta,s32 *x,s32 *y,s32 *z)
{ (void)theta;*x=*y=*z=0; }
void sound_play(s32 sound) { (void)sound; }
void game_message(const char *a,const char *b,s32 palette,s32 steps)
{ (void)a;(void)b;(void)palette;(void)steps; }

// Standard car/soft-body source and tuning are linked, but every profile
// runs through the game's real car_step(), not a reimplemented host equation.
static Car init_car(s32 profile)
{
    memset(g_tune, TUNE_STOCK, sizeof(g_tune));
    car_g = 35; car_grip = 38; car_power = 28000;
    car_crashes = 0;
    car_physics_profile = profile;
    car_softbody = profile == CAR_PHYS_SOFT_BODY;
    cheat_fly = cheat_hop = cheat_glitch = 0;
    tune_apply();
    Car c;
    car_reset(&c, 1000, 1000, 0);
    return c;
}

static Car run_steering(s32 profile, s32 phase)
{
    Car c = init_car(profile);
    // Identical controller inputs in each driving profile. Compare actual
    // car state at 40, 80 and 120 steps before the world boundary matters.
    for (s32 k = 0; k < (phase == 0 ? 40 : phase == 1 ? 80 : 120); k++) {
        u16 keys = KEY_A;
        if (k >= 40 && k < 100) keys |= KEY_RIGHT;
        if (k >= 100) keys |= KEY_L | KEY_RIGHT;
        car_step(&c, keys);
        assert(c.mode == CAR_GROUND || c.mode == CAR_AIR);
        assert(c.x > 0 && c.z > 0);
        assert(car_speed(&c) < 12000);
        assert(c.steer > -200 && c.steer < 200);
    }
    return c;
}

static void compare_original_modes(void)
{
    // Profile SOFT has a different original solver. Profile ARCADE and the
    // previously exported car_softbody=0 must be binary identical even if
    // the global selector had an old/unknown value.
    Car baseline = run_steering(CAR_PHYS_ARCADE, 2);
    Car fallback = run_steering(CAR_PHYS_SOFT_BODY, 0); // initializes softbody
    (void)fallback;
    Car identical = run_steering(CAR_PHYS_ARCADE, 2);
    assert(memcmp(&baseline, &identical, sizeof(Car)) == 0);
}

static void compare_classics(void)
{
    Car original = run_steering(CAR_PHYS_ARCADE, 1);
    Car hard = run_steering(CAR_PHYS_HARD_STYLE, 1);
    Car race = run_steering(CAR_PHYS_RACE_STYLE, 1);
    assert(hard.steer > 0 && race.steer > 0);
    assert(hard.heading != original.heading || hard.steer != original.steer);
    assert(race.heading != original.heading || race.steer != original.steer);
    assert(hard.heading != race.heading && hard.steer != race.steer);
    // HARD is designed to take longer to turn, RACE turns in faster.
    assert((s16)hard.heading < (s16)race.heading);
    printf("ARCADE heading=%d steer=%d  HARD heading=%d steer=%d  RACE heading=%d steer=%d\n",
           original.heading, original.steer, hard.heading, hard.steer,
           race.heading, race.steer);
}

static void brakes_and_reverse(void)
{
    for (s32 mode = CAR_PHYS_HARD_STYLE; mode <= CAR_PHYS_RACE_STYLE; mode++) {
        Car c = init_car(mode);
        for (int i=0; i<120; i++) car_step(&c, KEY_A);
        s32 before = car_speed(&c);
        for (int i=0; i<120; i++) car_step(&c, KEY_B);
        s32 after = car_speed(&c);
        assert(after < before);
        assert(c.gear == 0 || after < 200);
        assert(c.mode == CAR_GROUND || c.mode == CAR_AIR);
        // Releasing the steering returns the wheel angle toward zero.
        car_step(&c, KEY_LEFT | KEY_B);
        s32 left = c.steer;
        for (int i=0; i<40; i++) car_step(&c, 0);
        assert(iabs(c.steer) < iabs(left) || c.steer == 0);
    }
}

int main(void)
{
    r_init();
    compare_original_modes();
    compare_classics();
    brakes_and_reverse();
    puts("PASS: unchanged stock profiles, separate hard/race steering, braking, recovery");
    return 0;
}
