// Production race, car and soft-body rules on a deterministic flat circuit.
// Rendering, world queries and persistence are fixture seams. Assertions
// specify movement, crossing order and earned progress at recovery.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "race.c"
#include "tune.h"

const Loop the_loop = {0};
static s32 messages, saved;
s32 world_height(s32 x, s32 z) { (void)x; (void)z; return 0; }
s32 world_surface(s32 x, s32 z) { (void)x; (void)z; return SURF_ROAD; }
s32 world_in_water(s32 x, s32 z) { (void)x; (void)z; return 0; }
s32 world_collide(s32 x, s32 z, s32 radius, s32 *nx, s32 *nz)
{ (void)x; (void)z; (void)radius; *nx = *nz = 0; return 0; }
s32 world_ground_plane(s32 x, s32 z, s32 r, s32 *h, s32 *gx, s32 *gz)
{ (void)x; (void)z; (void)r; *h = *gx = *gz = 0; return 1; }
void loop_point(s32 theta, s32 *x, s32 *y, s32 *z)
{ (void)theta; *x = *y = *z = 0; assert(0 && "Circuit cannot enter city loop"); }
void sound_play(s32 sound) { (void)sound; }
void game_message(const char *a, const char *b, s32 palette, s32 steps)
{ (void)a; (void)b; (void)palette; (void)steps; messages++; }
void save_blob(u32 magic, u32 at, const void *data, u32 size)
{ (void)magic; (void)at; (void)data; assert(size == 24); saved++; }


static void stock_finish(s32 rival_need, s32 rival_speed, s32 line_target, s32 position)
{
    track_load(0);
    Car car;
    race_begin(0, RACE_ARCADE, &car);
    s32 goal = g_race.laps * g_track->lap;
    g_race.lap = g_race.laps - 1;
    g_race.cp_given = g_race.laps;
    g_race.time_left = 3600;
    g_race.total_steps = 101;
    const TrackPt *first = &g_track->pts[0];
    const TrackPt *last = &g_track->pts[g_track->count - 1];
    assert(first->x == 4000 && first->z == 5200 && first->ux == -66 && first->uz == 16384);
    assert(last->dist + last->len == 32564 && g_track->lap == 32576);
    car.x = 4000 * 256;
    car.z = 5200 * 256 - 1408;
    car.vz = 2048;
    car.heading = 0;
    car_softbody = 0;
    TrackHit prev;
    assert(track_find(car.x >> 8, car.z >> 8, &prev));
    g_race.last_d = g_track->pts[prev.seg].dist + prev.along;
    g_race.progress = goal - (g_track->lap - g_race.last_d);
    for (s32 i = 0; i < RIVALS; i++) rivals[i].s = 1000 * 256;
    race_step(&car, 0); // Capture the last actual position before movement.
    Rival *r = &rivals[0];
    r->s = goal * 256 - rival_need;
    r->v = rival_speed;
    r->skill = 244;
    r->lat = r->lat_target = 125;
    r->timer = 100;
    r->cap = 0x7fff;
    for (s32 i = 0; i < g_track->count; i++) line_speed[i] = line_target;
    rival_pose(r);
    s32 z_before = car.z, rival_before = r->s;
    car_step(&car, KEY_A);
    race_step(&car, 1);
    assert(car.x == first->x * 256 && car.z - z_before == 2057);
    assert(r->s - rival_before == rival_speed);
    assert(g_race.state == RS_GOAL && g_race.position == position);
    for (s32 i = 0; i < RIVALS; i++) rivals[i].s = (goal + 1000) * 256;
    race_step(&car, 1);
    assert(g_race.position == position); // Freeze the earned result.
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    r_init();
    memset(g_tune, TUNE_STOCK, sizeof(g_tune));
    tune_apply();
    if (!strcmp(argv[1], "player-first")) stock_finish(1700, 1952, 2048, 1);
    else if (!strcmp(argv[1], "rival-first")) stock_finish(1000, 1952, 2048, 2);
    else if (!strcmp(argv[1], "tie")) stock_finish(1408, 2057, 2159, 1);
    else if (!strcmp(argv[1], "prior-rival")) stock_finish(-256, 1952, 2048, 2);
    else assert(0 && "Unknown finish case");
    printf("PASS: stock finish %s\n", argv[1]);
    return 0;
}
