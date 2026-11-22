// Production traffic rules on a synthetic circular road. Lap offsets must
// affect standings and rubber banding, but not the same physical encounter.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "race.c"
#include "tune.h"

static const TrackPt points[] = {{.len = 10000, .radius = 8000}};
#define COURSE {.pts = points, .count = 1, .lap = 10000, .laps = 3, .cp_time = 32}
const TrackDef g_tracks[TRACK_COUNT] = {
    COURSE, COURSE, COURSE,
#ifdef WITH_VR_ORIGINAL_TRACKS
    COURSE, COURSE, COURSE,
#endif
};
const TrackDef *g_track;
s16 track_pitch[256];
#ifdef WITH_VR_ORIGINAL_TRACKS
const u32 g_vr_original_pack_version = 1;
#endif
s32 car_g = 35, car_grip = 38, car_power = 28000, car_crashes = 1;
s32 cheat_fly, cheat_hop, cheat_glitch;
static s32 next_distance, projection_available;

s32 track_find(s32 x, s32 z, TrackHit *hit)
{
    (void)x; (void)z;
    if (!projection_available) return 0;
    *hit = (TrackHit){.along = next_distance};
    return 1;
}
s32 track_half_width(s32 segment) { (void)segment; return 150; }
s32 track_height(s32 x, s32 z) { (void)x; (void)z; return 0; }
s32 track_height_near(s32 segment, s32 x, s32 z)
{
    (void)segment; (void)x; (void)z; return 0;
}
void track_gradient(s32 x, s32 z, s32 *gx, s32 *gz)
{
    (void)x; (void)z; *gx = *gz = 0;
}
void track_point(s32 d, s32 lat, s32 *x, s32 *z, s32 *y, s32 *heading, s32 *hint)
{
    d %= 10000;
    if (d < 0) d += 10000;
    s32 angle = d * 65536 / 10000;
    *x = 5000 + (((1600 - lat) * isin(angle >> 6)) >> 14);
    *z = 5000 + (((1600 - lat) * icos(angle >> 6)) >> 14);
    *heading = (u16)(angle + 16384);
    *y = *hint = 0;
}
s32 world_height(s32 x, s32 z) { return track_height(x, z); }
s32 world_surface(s32 x, s32 z) { (void)x; (void)z; return SURF_ROAD; }
s32 world_in_water(s32 x, s32 z) { (void)x; (void)z; return 0; }
s32 world_collide(s32 x, s32 z, s32 radius, s32 *nx, s32 *nz)
{
    (void)x; (void)z; (void)radius; *nx = *nz = 0; return 0;
}
s32 world_ground_plane(s32 x, s32 z, s32 r, s32 *h, s32 *gx, s32 *gz)
{
    (void)x; (void)z; (void)r; *h = *gx = *gz = 0; return 1;
}
static s32 speed_queries;
s32 car_speed(const Car *car)
{
    speed_queries++;
    return isqrt(car->vx * car->vx + car->vz * car->vz);
}
// The traffic harness does not link car.c; only its safe initial pose is
// needed here to exercise production race_begin, not car physics.
void car_reset(Car *car, s32 x, s32 z, s32 heading)
{
    memset(car, 0, sizeof(*car));
    car->x = x << 8;
    car->z = z << 8;
    car->heading = heading;
    car->safe_x = x;
    car->safe_z = z;
    car->safe_heading = heading;
}
void car_respawn(Car *car) { (void)car; assert(0 && "Flat traffic fixture cannot respawn"); }
void sound_play(s32 sound) { (void)sound; }
void game_message(const char *a, const char *b, s32 palette, s32 steps)
{
    (void)a; (void)b; (void)palette; (void)steps;
}
void save_blob(u32 magic, u32 at, const void *data, u32 size)
{
    (void)magic; (void)at; (void)data; (void)size;
}

static Car reset_case(s32 course, s32 progress, s32 rival_progress, s32 speed)
{
    memset(&g_race, 0, sizeof(g_race));
    memset(rivals, 0, sizeof(rivals));
    sb_invalidate();
    g_track = &g_tracks[course];
    r_far = 1800;
    race_half_width = 150;
    g_race.mode = RACE_ARCADE;
    g_race.track = course;
    g_race.laps = 3;
    g_race.progress = progress;
    g_race.lap = progress / 10000;
    g_race.cp_given = g_race.lap;
    g_race.last_d = next_distance = progress % 10000;
    projection_available = 1;
    g_race.time_left = 10000;
    for (s32 i = 0; i < RIVALS; i++) {
        rivals[i].s = (progress + 3000 + i * 300) * 256;
        rivals[i].timer = 100;
        rivals[i].cap = 0x7fff;
        rivals[i].skill = 256;
    }
    rivals[0].s = rival_progress * 256;
    line_speed[0] = 3000;
    s32 x, z, y, heading, hint = 0;
    track_point(progress, 0, &x, &z, &y, &heading, &hint);
    return (Car){.x = x * 256, .y = y, .z = z * 256, .heading = heading,
                 .vx = (speed * isin(heading >> 6)) >> 14,
                 .vz = (speed * icos(heading >> 6)) >> 14, .mode = CAR_GROUND};
}

static void contacts(s32 course, s32 softbody)
{
    // Behind, level and ahead contacts, also straddling the start line.
    static const s32 progress[] = {11000, 20005, 29995};
    static const s32 gaps[] = {-20, 0, 20};
    for (u32 p = 0; p < sizeof(progress) / sizeof(progress[0]); p++)
        for (u32 gap = 0; gap < sizeof(gaps) / sizeof(gaps[0]); gap++) {
            s32 reference_hit = -1, reference_lat = 0;
            for (s32 lap = 0; lap <= 2; lap++) {
                // First the ordinary scene, then the same rival one lap
                // behind/ahead. Neither direction may turn it into a ghost.
                s32 offset = lap == 1 ? -10000 : lap == 2 ? 10000 : 0;
                Car car = reset_case(course, progress[p], progress[p] + gaps[gap] + offset,
                                     gaps[gap] < 0 ? 0 : 3000);
                if (softbody) sb_reset(&car);
                rivals[0].v = 1024;
                s32 x = car.x, z = car.z;
                rivals_step(&car, 0, next_distance, 0);
                if (softbody) assert(car.hit > 0);
                else assert(car.x != x || car.z != z);
                assert(rivals[0].v == (gaps[gap] < 0 ? 960 : 1064));
                if (!lap) { reference_hit = car.hit; reference_lat = rivals[0].lat; }
                else assert(car.hit == reference_hit && rivals[0].lat == reference_lat);
            }
        }
}

static void following(s32 course, s32 cpu_obstacle)
{
    // A stopped car 20 units ahead requires braking even across a lap or
    // the start line. Keep the player high enough to isolate following.
    for (s32 lap = -2; lap <= 2; lap++) {
        Car car = reset_case(course, cpu_obstacle ? 13000 : 20005,
                             cpu_obstacle ? 19985 : 19985 + lap * 10000, 0);
        car.y = 100 * 256;
        rivals[0].v = 1200;
        if (cpu_obstacle) rivals[1].s = (20005 + lap * 10000) * 256;
        rivals_step(&car, 1, next_distance, 0);
        assert(rivals[0].cap < 0 && rivals[0].v < 1200);
        assert(rivals[0].lat_target > 0);
    }
}

static void slipstream(s32 course)
{
    for (s32 lap = -2; lap <= 2; lap++) {
        Car car = reset_case(course, 19950, 20050 + lap * 10000, 2000);
        s32 vx = car.vx, vz = car.vz;
        rivals_step(&car, 0, next_distance, 0);
        assert(g_race.slipstream == 1);
        assert(car.vx == vx + (vx >> 10) && car.vz == vz + (vz >> 10));
    }
    Car car = reset_case(course, 20050, 19950, 2000);
    rivals_step(&car, 0, next_distance, 0);
    assert(!g_race.slipstream); // following cars do not provide a tow
    car = reset_case(course, 19950, 20050, 2000);
    rivals[0].lat = 80;
    rivals_step(&car, 0, next_distance, 0);
    assert(!g_race.slipstream); // traffic in another lane is not a tow
}

static void far_traffic(s32 course)
{
    Car car = reset_case(course, 11000, 14000, 2000);
    car.y = 100 * 256;
    rivals[0].x = 123;
    rivals[0].z = 456;
    g_race.total_steps = 1;
    rivals_step(&car, 1, next_distance, 0);
    assert(rivals[0].x == 123 && rivals[0].z == 456); // distant pose remains throttled
    assert(!car.hit && !g_race.slipstream);
    assert(rivals[0].v == 13 && rivals[0].cap == 0x7fff);
}

static Car draft_run(s32 course, s32 drafting)
{
    Car car = reset_case(course, 11000, 11300, 2000);
    sb_reset(&car);
    // Eight real steps per run keep the solver's periodic maintenance on
    // the same phase. The distant rival cannot touch during this check.
    for (s32 i = 0; i < 7; i++) sb_step(&car, KEY_A);
    if (!drafting) rivals[0].lat = 80;
    rivals_step(&car, 0, next_distance, 0);
    assert(g_race.slipstream == drafting && !car.hit);
    sb_step(&car, KEY_A);
    return car;
}

static void softbody_draft(s32 course)
{
    Car ordinary = draft_run(course, 0), drafted = draft_run(course, 1);
    s32 s = isin(ordinary.heading >> 6), c = icos(ordinary.heading >> 6);
    assert((drafted.x - ordinary.x) * s + (drafted.z - ordinary.z) * c > 0);
    assert((drafted.vx - ordinary.vx) * s + (drafted.vz - ordinary.vz) * c > 0);
}

static void standings(s32 course)
{
    // Both opponents are physically behind, but only one has completed
    // more of the race. Wrapped physical gaps must not become standings.
    Car car = reset_case(course, 11000, 20900, 0);
    car.y = 100 * 256;
    for (s32 i = 1; i < RIVALS; i++) rivals[i].s = 900 * 256;
    race_step(&car, 1);
    assert(g_race.position == 2 && g_race.progress == 11000);
    // A lap-ahead car still gets the existing leader slowdown even if it
    // is physically alongside. The cap is skipped on this phase.
    car = reset_case(course, 11000, 21000, 0);
    car.y = 100 * 256;
    rivals[0].v = 2700;
    g_race.total_steps = 1;
    rivals_step(&car, 1, next_distance, 0);
    assert(rivals[0].v == 2670);
}

static Car rejoin_case(s32 course, s32 rival_progress, s32 speed)
{
    Car car = reset_case(course, 1500, rival_progress, speed);
    // A reset moves the car back 500 units. Its next projection must not
    // award or remove that much race progress, but traffic is now at 1500.
    g_race.progress = g_race.last_d = 2000;
    for (s32 i = 1; i < RIVALS; i++) rivals[i].s = 1000 * 256;
    return car;
}

static void rejoin_draft(s32 course)
{
    Car car = rejoin_case(course, 1600, 2000);
    s32 vx = car.vx, vz = car.vz;
    race_step(&car, 1);
    assert(g_race.slipstream == 1);
    assert(car.vx == vx + (vx >> 10) && car.vz == vz + (vz >> 10));
    assert(g_race.progress == 2000 && g_race.last_d == 1500 && g_race.position == 1);
    // Ordinary movement after rejoining still counts, retaining the offset
    // caused by the ignored reset rather than silently replacing race credit.
    next_distance = 1510;
    race_step(&car, 1);
    assert(g_race.progress == 2010 && g_race.last_d == 1510 && g_race.position == 1);
}

static void rejoin_following(s32 course)
{
    Car car = rejoin_case(course, 1400, 0);
    car.y = 100 * 256; // isolate following from collision resolution
    rivals[0].v = 1200;
    race_step(&car, 1);
    assert(rivals[0].cap < 0 && rivals[0].v == 1170);
    assert(g_race.progress == 2000 && g_race.last_d == 1500);
}

static void rejoin_culling(s32 course)
{
    Car car = rejoin_case(course, 1600, 2000);
    // A larger relocation would otherwise classify a nearby rival as far
    // and keep its stale pose on this maintenance phase.
    g_race.progress = g_race.last_d = 4000;
    g_race.total_steps = 1;
    rivals[0].x = 123;
    rivals[0].z = 456;
    race_step(&car, 1);
    assert(rivals[0].x != 123 && rivals[0].z != 456 && g_race.slipstream == 1);
    assert(g_race.progress == 4000 && g_race.last_d == 1500);
}

static void missing_projection(s32 course)
{
    Car car = rejoin_case(course, 1600, 2000);
    g_race.last_d = 1500; // last successful lookup after a prior relocation
    projection_available = 0;
    race_step(&car, 1);
    assert(g_race.slipstream == 1);
    assert(g_race.progress == 2000 && g_race.last_d == 1500);
}

static void rejoin_standings(s32 course)
{
    Car car = rejoin_case(course, 2600, 0);
    car.y = 100 * 256;
    g_race.total_steps = 1;
    rivals[0].v = 2700;
    race_step(&car, 1);
    // Only 600 units ahead in the race: no leader slowdown, even though
    // the physical gap from the relocated car is 1100 units.
    assert(rivals[0].v == 2713);
    assert(g_race.position == 2 && g_race.progress == 2000 && g_race.last_d == 1500);
}


static void free_run_skips_racing_work(s32 course)
{
    Car car = reset_case(course, 1000, 1300, 2000);
    // Free Run should not build CPU speed targets or opponent poses.
    line_speed[0] = 1234;
    rivals[0].s = 71123;
    race_begin(course, RACE_FREE, &car);
    assert(g_race.mode == RACE_FREE && g_race.position == 1);
    assert(line_speed[0] == 1234 && rivals[0].s == 71123);

    g_race.slipstream = 1;
    speed_queries = 0;
    s32 x = car.x, z = car.z, vx = car.vx, vz = car.vz;
    rivals_step(&car, 1, next_distance, 0);
    assert(speed_queries == 0 && g_race.slipstream == 0);
    assert(car.x == x && car.z == z && car.vx == vx && car.vz == vz);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    r_init();
    memset(g_tune, 10, sizeof(g_tune));
    tune_apply();
    for (s32 set = 0; set < TRACK_SET_COUNT; set++) {
        s32 course = set * TRACKS_PER_SET;
        if (!strcmp(argv[1], "contact-softbody")) contacts(course, 1);
        else if (!strcmp(argv[1], "contact-arcade")) contacts(course, 0);
        else if (!strcmp(argv[1], "following-player")) following(course, 0);
        else if (!strcmp(argv[1], "following-rival")) following(course, 1);
        else if (!strcmp(argv[1], "slipstream")) slipstream(course);
        else if (!strcmp(argv[1], "softbody-draft")) softbody_draft(course);
        else if (!strcmp(argv[1], "far")) far_traffic(course);
        else if (!strcmp(argv[1], "free-run-skip")) free_run_skips_racing_work(course);
        else if (!strcmp(argv[1], "standings")) standings(course);
        else if (!strcmp(argv[1], "rejoin-draft")) rejoin_draft(course);
        else if (!strcmp(argv[1], "rejoin-following")) rejoin_following(course);
        else if (!strcmp(argv[1], "rejoin-culling")) rejoin_culling(course);
        else if (!strcmp(argv[1], "projection-missing")) missing_projection(course);
        else if (!strcmp(argv[1], "rejoin-standings")) rejoin_standings(course);
        else assert(0 && "Unknown traffic regression");
    }
    printf("PASS: %s physical traffic and total race progress\n", argv[1]);
    return 0;
}
