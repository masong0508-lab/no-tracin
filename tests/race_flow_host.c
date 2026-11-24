// Production race, car and soft-body rules on a deterministic flat circuit.
// Rendering, world queries and persistence are fixture seams. Assertions
// specify movement, crossing order and earned progress at recovery.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "race.c"
#include "tune.h"

static TrackPt points[] = {{.x = 5000, .z = 4000, .uz = 16384,
    .len = 10000, .radius = 8000, .heading = 0}};
#define COURSE {.name = "PROBE", .pts = points, .count = 1, .lap = 10000, \
    .laps = 3, .start_time = 64, .cp_time = 32}
const TrackDef g_tracks[TRACK_COUNT] = {
    COURSE, COURSE, COURSE,
#ifdef WITH_VR_ORIGINAL_TRACKS
    COURSE, COURSE, COURSE,
#endif
#ifdef WITH_VRD_32X_TRACKS
    COURSE, COURSE, COURSE, COURSE, COURSE,
#endif
};
const TrackDef *g_track;
#ifdef WITH_VRD_32X_TRACKS
// Route fixture has no private generated original source height data.
s32 vrd32x_study_slot(void){return -1;}
void vrd32x_study_set_height_reference(s32 h){(void)h;}
#endif
s16 track_pitch[256];
#ifdef WITH_VR_ORIGINAL_TRACKS
const u32 g_vr_original_pack_version = 0x11223344;
#endif
const Loop the_loop = {0};
static s32 messages, saved, projection_available = 1;
#if defined(WITH_VR_ORIGINAL_TRACKS) || defined(WITH_VRD_32X_TRACKS)
static s32 road_covered = 1;
s32 track_surface(s32 x, s32 z)
{ (void)x; (void)z; return road_covered ? SURF_ROAD : SURF_GRASS; }
#endif

s32 track_find(s32 x, s32 z, TrackHit *hit)
{
    if (!projection_available) return 0;
    s32 d = (z - 4000) % 10000;
    if (d < 0) d += 10000;
    s32 lat = x - 5000;
    *hit = (TrackHit){.along = d, .lat = lat, .dist = lat < 0 ? -lat : lat};
    return 1;
}
// Artificial track fixture: route-only lookup shares its known geometry.
s32 track_find_road(s32 x, s32 z, TrackHit *hit) { return track_find(x, z, hit); }
s32 track_half_width(s32 segment) { (void)segment; return 150; }
s32 track_native_half_width(s32 segment, s32 lateral)
{ (void)segment; (void)lateral; return 150; }
s32 track_height(s32 x, s32 z) { (void)x; (void)z; return 0; }
s32 track_height_near(s32 segment, s32 x, s32 z)
{ (void)segment; (void)x; (void)z; return 0; }
void track_gradient(s32 x, s32 z, s32 *gx, s32 *gz)
{ (void)x; (void)z; *gx = *gz = 0; }
void track_point(s32 d, s32 lat, s32 *x, s32 *z, s32 *y, s32 *heading, s32 *hint)
{
    d %= 10000;
    if (d < 0) d += 10000;
    *x = 5000 + lat; *z = 4000 + d; *y = *heading = *hint = 0;
}
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

static Car reset_case(s32 course, s32 mode, s32 progress)
{
    memset(&g_race, 0, sizeof(g_race));
    memset(recs, 0, sizeof(recs));
    memset(rivals, 0, sizeof(rivals));
    sb_invalidate();
    projection_available = 1;
    g_track = &g_tracks[course];
    r_far = 1800;
    race_half_width = 150;
    g_race.mode = mode;
    g_race.track = course;
    g_race.laps = mode == RACE_ARCADE ? 3 : 9999;
    g_race.progress = progress;
    g_race.last_d = progress % 10000;
    g_race.lap = progress / 10000;
    g_race.cp_given = g_race.lap + (progress % 10000 >= 5000);
    g_race.time_left = 100000;
    g_race.lap_steps = 100;
    g_race.position = 1;
    for (s32 i = 0; i < RIVALS; i++) {
        rivals[i].s = 1000 * 256;
        rivals[i].timer = 100;
        rivals[i].cap = 0x7fff;
        rivals[i].lat = 100;
        rivals[i].lat_target = 100;
    }
    line_speed[0] = 3000;
    messages = saved = 0;
    Car car;
    car_reset(&car, 5000, 4000 + progress % 10000, 0);
    return car;
}

// Regression expectations are physical crossing order and road coordinates,
// independent of the proposed implementation. Production source stays real.

static void set_road(Car *car, s32 progress)
{
    car->z = (4000 + (progress % 10000 + 10000) % 10000) * 256;
    race_step(car, 1);
}

static void wrong_way_motion(s32 course)
{
    for (s32 softbody = 0; softbody <= 1; softbody++)
        for (s32 against = 0; against <= 1; against++) {
            Car car = reset_case(course, RACE_FREE, 5000);
            car_softbody = softbody;
            car.heading = against ? 32768 : 0;
            for (s32 step = 0; step < 180; step++) {
                car_step(&car, KEY_B);
                race_step(&car, 1);
            }
            assert(car.gear == 0 && car_speed(&car) > 300);
            if (against) {
                assert(g_race.progress > 5100);
                assert(!g_race.wrong_way); // Reverse gear, correct travel.
            } else {
                assert(g_race.progress < 4900);
                assert(g_race.wrong_way);  // Reverse gear, backward travel.
            }
        }
}

static void wrong_way_cone(s32 course)
{
    // Literal velocities around the original ~120-degree backwards cone.
    static const struct { s32 vx, vz, mode, want; } cases[] = {
        {0, 800, CAR_GROUND, 0}, {0, -800, CAR_GROUND, 1},
        {800, 0, CAR_GROUND, 0}, {752, -274, CAR_GROUND, 0}, // ~110 degrees
        {613, -514, CAR_GROUND, 1}, // ~130 degrees
        {0, -300, CAR_GROUND, 0}, {0, -301, CAR_GROUND, 1},
        {0, 0, CAR_GROUND, 0}, {0, -800, CAR_AIR, 0},
    };
    for (s32 turn = 0; turn <= 1; turn++)
        for (u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            Car car = reset_case(course, RACE_FREE, 5000);
            points[0].ux = turn ? 16384 : 0;
            points[0].uz = turn ? 0 : 16384;
            points[0].heading = turn ? 16384 : 0;
            // Body deliberately points incorrectly for the forward case.
            car.heading = (u16)(points[0].heading + 32768);
            car.vx = turn ? cases[i].vz : cases[i].vx;
            car.vz = turn ? -cases[i].vx : cases[i].vz;
            car.mode = cases[i].mode;
            for (s32 step = 0; step < 45; step++) {
                race_step(&car, 1);
                assert(!g_race.wrong_way);
            }
            race_step(&car, 1);
            assert(g_race.wrong_way == cases[i].want);
        }
    points[0].ux = 0; points[0].uz = 16384; points[0].heading = 0;
    Car car = reset_case(course, RACE_FREE, 5000);
    car.vz = -800;
    for (s32 step = 0; step < 45; step++) race_step(&car, 1);
    car.vz = 800;
    race_step(&car, 1); // A correct-direction tick clears the pending warning.
    assert(!g_race.wrong_way && !g_race.wrong_steps);
    car.vz = -800;
    for (s32 step = 0; step < 45; step++) race_step(&car, 1);
    assert(!g_race.wrong_way);
    race_step(&car, 1);
    assert(g_race.wrong_way);
    car.vz = 800;
    race_step(&car, 1);
    assert(!g_race.wrong_way);
}


static void wrong_way_projection_loss(s32 course)
{
    // Losing the road projection must clear a visible or pending warning.
    // Reacquiring the road must require a fresh 46 reverse-motion steps.
    for (s32 mode = RACE_ARCADE; mode <= RACE_FREE; mode++) {
        Car car = reset_case(course, mode, 5000);
        car.mode = CAR_GROUND;
        car.vx = 0;
        car.vz = -800;
        for (s32 i = 0; i < 46; i++) race_step(&car, 1);
        assert(g_race.wrong_way && g_race.wrong_steps == 46);
        s32 progress = g_race.progress, road = g_race.last_d;

        projection_available = 0;
        race_step(&car, 0); // Also clear the warning during a countdown.
        assert(!g_race.wrong_way && g_race.wrong_steps == 0);
        assert(g_race.progress == progress && g_race.last_d == road);

        projection_available = 1;
        for (s32 i = 0; i < 45; i++) {
            race_step(&car, 1);
            assert(!g_race.wrong_way);
        }
        assert(g_race.wrong_steps == 45);
        race_step(&car, 1);
        assert(g_race.wrong_way);

        // Partial reverse motion must also be forgotten after losing track.
        car.vz = 800;
        race_step(&car, 1);
        assert(!g_race.wrong_way && g_race.wrong_steps == 0);
        car.vz = -800;
        for (s32 i = 0; i < 20; i++) race_step(&car, 1);
        assert(!g_race.wrong_way && g_race.wrong_steps == 20);
        projection_available = 0;
        race_step(&car, 1);
        assert(!g_race.wrong_way && g_race.wrong_steps == 0);
    }
}

static Car finish_case(s32 course, s32 player_before, s32 rival_before_q8,
                       s32 line_target, s32 rival_speed)
{
    Car car = reset_case(course, RACE_ARCADE, player_before);
    car.x = 4900 * 256; // Rival lane is +100; no contact/following confound.
    rivals[0].s = rival_before_q8;
    rivals[0].skill = 244;
    rivals[0].v = rival_speed;
    line_speed[0] = line_target;
    race_step(&car, 0); // Establish the preceding observed position.
    return car;
}

static void finish_prior(s32 course)
{
    Car car = finish_case(course, 29998, 30000 * 256 - 64, 3000, 256);
    race_step(&car, 1);
    assert(rivals[0].s > 30000 * 256 && g_race.state == RS_RACING);
    assert(g_race.position == 2);
    set_road(&car, 30005);
    assert(g_race.state == RS_GOAL && g_race.position == 2);
    // Post-finish traffic motion must not change this result.
    for (s32 i = 0; i < RIVALS; i++) rivals[i].s = 40000 * 256;
    set_road(&car, 30010);
    assert(g_race.position == 2 && g_race.end_timer == 1);
}

static void finish_same(s32 course)
{
    // Rival: 1 unit / 3.8125 units = 0.2623 of this tick.
    // Player: 10 / 20 = 0.5; player ends farther ahead but finishes second.
    Car car = finish_case(course, 29990, 29999 * 256, 1024, 976);
    set_road(&car, 30010);
    assert(g_race.state == RS_GOAL && g_race.position == 2);

    // Player: 1 / 3 = 0.3333; rival: 4 / 7.625 = 0.5246.
    // Rival ends farther ahead but the player crossed first.
    car = finish_case(course, 29999, 29996 * 256, 2048, 1952);
    set_road(&car, 30002);
    assert(g_race.state == RS_GOAL && g_race.position == 1);
}

static void finish_tie(s32 course)
{
    // Both cross at exactly 0.5 tick. Rival ends 3.8125 past the line,
    // player ends 1 past it. Preserve the player-favored exact tie convention.
    Car car = finish_case(course, 29999, 30000 * 256 - 976, 2048, 1952);
    set_road(&car, 30001);
    assert(g_race.state == RS_GOAL && g_race.position == 1);
}

static void finish_subunit(s32 course)
{
    // The continuous start-line plane is z=4000. Keep its Q8 subunits;
    // neither the integer road projection nor the car's exported velocity
    // is an adequate substitute for the positions before/after movement.
    static const s32 needs[] = {122, 244, 488};
    static const s32 positions[] = {2, 1, 1};
    for (s32 i = 0; i < 3; i++) {
        Car car = finish_case(course, 29999, 30000 * 256 - needs[i], 2048, 1952);
        car.z = 4000 * 256 - 64;
        race_step(&car, 0);
        car.z += 512;
        // Deliberately unrelated velocity: the soft body's exported mean
        // need not equal displacement after constraint/contact resolution.
        car.vz = 17;
        race_step(&car, 1);
        assert(g_race.state == RS_GOAL && g_race.position == positions[i]);
    }
}

static Car safe_case(s32 course, s32 progress)
{
    Car car = reset_case(course, RACE_ARCADE, progress);
    car.x = 4900 * 256;
    // Let the real race step capture its own safe pose/progress snapshot.
    race_step(&car, 1);
    assert(car.safe_x == 5000 && car.safe_z == 4000 + progress % 10000);
    car.mode = CAR_AIR;
    return car;
}

static void respawn_back(s32 course)
{
    Car car = safe_case(course, 1000);
    for (s32 d = 1100; d <= 1300; d += 100) set_road(&car, d);
    car_respawn(&car);
    assert(g_race.progress == 1000 && g_race.last_d == 1000);
    race_step(&car, 1);
    s32 cp_at = -1, lap_at = -1;
    for (s32 d = 1001; d <= 10000; d++) {
        set_road(&car, d);
        if (cp_at < 0 && g_race.cp_given == 1) cp_at = g_race.last_d;
        if (g_race.lap == 1) { lap_at = g_race.last_d; break; }
    }
    assert(cp_at == 5000 && lap_at == 0);
}

static void respawn_cp(s32 course)
{
    Car car = safe_case(course, 4900);
    set_road(&car, 5000);
    set_road(&car, 5100);
    assert(g_race.cp_given == 1);
    s32 clock = g_race.time_left, old_messages = messages;
    car_respawn(&car);
    assert(g_race.progress == 4900 && g_race.cp_given == 1);
    race_step(&car, 1);
    set_road(&car, 5000);
    set_road(&car, 5100);
    assert(g_race.time_left == clock - 3 && messages == old_messages && g_race.cp_given == 1);
}

static void respawn_lap(s32 course)
{
    Car car = safe_case(course, 9900);
    set_road(&car, 10000);
    set_road(&car, 10100);
    assert(g_race.lap == 1 && saved == 1);
    s32 clock = g_race.time_left, lap_time = g_race.lap_times[0];
    car_respawn(&car);
    assert(g_race.progress == 9900 && g_race.last_d == 9900 && g_race.lap == 1);
    race_step(&car, 1);
    set_road(&car, 10000);
    set_road(&car, 10100);
    assert(g_race.lap == 1 && saved == 1 && g_race.time_left == clock - 3);
    assert(g_race.lap_times[0] == lap_time);
}

static void respawn_forward(s32 course)
{
    // Reverse across an already-earned checkpoint, then respawn forward.
    Car car = safe_case(course, 5100);
    set_road(&car, 5000); set_road(&car, 4900); set_road(&car, 4800);
    s32 clock = g_race.time_left, old_messages = messages;
    car_respawn(&car);
    assert(g_race.progress == 5100 && g_race.last_d == 5100 && g_race.cp_given == 1);
    race_step(&car, 1);
    assert(g_race.time_left == clock - 1 && messages == old_messages && g_race.lap == 0);

    // Same recovery across the start-line wrap; do not invent a second lap.
    car = safe_case(course, 10100);
    set_road(&car, 10000); set_road(&car, 9900); set_road(&car, 9800);
    assert(g_race.progress == 9800 && g_race.lap == 1);
    clock = g_race.time_left;
    car_respawn(&car);
    assert(g_race.progress == 10100 && g_race.last_d == 100 && g_race.lap == 1);
    race_step(&car, 1);
    assert(g_race.time_left == clock - 1 && g_race.lap == 1 && saved == 0);
}

static void respawn_grid(s32 course)
{
    Car car = reset_case(course, RACE_ARCADE, 1000);
    race_begin(course, RACE_ARCADE, &car);
    car.mode = CAR_AIR;
    set_road(&car, 9338); set_road(&car, 9438); set_road(&car, 9538);
    assert(g_race.progress == -462);
    car_respawn(&car);
    assert(g_race.progress == -762 && g_race.last_d == 9238);
    assert(!g_race.lap && !g_race.cp_given);
}

static void jump_guard(s32 course)
{
    Car car = safe_case(course, 1000);
    set_road(&car, 1400); // No respawn: ordinary large-jump guard remains.
    assert(g_race.progress == 1000 && g_race.last_d == 1400 && !g_race.cp_given);
}


#ifdef WITH_VR_ORIGINAL_TRACKS
static void original_branch_recovery(void)
{
    // Synthetic source asphalt and original main route: the side road
    // is real, but its main-route projection is 1300 units away.
    Car car = reset_case(TRACKS_PER_SET, RACE_FREE, 5000);
    road_covered = 1;
    car.mode = CAR_GROUND;
    car.x = 6300 * 256;
    car.z = 5020 * 256;
    car.heading = 12420;
    car.vz = -800;
    g_race.wrong_way = 1;
    g_race.wrong_steps = 100;
    s32 initial_progress = g_race.progress, initial_d = g_race.last_d;
    for (s32 i = 0; i < 16; i++) race_step(&car, 1);
    assert(g_race.progress == initial_progress && g_race.last_d == initial_d);
    assert(!g_race.wrong_way && !g_race.wrong_steps);
    assert(car.safe_x == 6300 && car.safe_z == 5020);
    assert(car.safe_heading == 12420);
    assert(!saved && !messages);

    car.x = 8000 * 256;
    car.z = 8100 * 256;
    car_respawn(&car);
    assert(car.x == 6300 * 256 && car.z == 5020 * 256);
    assert(car.heading == 12420);
    assert(g_race.progress == initial_progress && g_race.last_d == initial_d);

    // An unsupported/grass surface, even with a nearby guessed main
    // route, must never become a new source-backed safe position.
    road_covered = 0;
    car.x = 6600 * 256;
    car.z = 5400 * 256;
    for (s32 i = 0; i < 16; i++) race_step(&car, 1);
    assert(car.safe_x == 6300 && car.safe_z == 5020);
    road_covered = 1;
}
#endif

#ifdef WITH_VRD_32X_TRACKS
static void study_free_drive(void)
{
    Car car = reset_case(TRACK_VRD_FIRST, RACE_FREE, 9990);
    road_covered = 1;
    car.mode = CAR_GROUND;
    car.x = 6300 * 256; // A verified side branch, not the staging centreline.
    car.z = 4020 * 256;
    car.heading = 12420;
    // Unverified direction from the skeleton must never show a warning.
    g_race.wrong_way = 1;
    g_race.wrong_steps = 100;
    const s32 original_cp = g_race.cp_given;
    for (s32 i = 0; i < 16; i++) race_step(&car, 1);
    assert(!g_race.wrong_way && !g_race.wrong_steps);
    assert(g_race.total_steps == 16);
    assert(car.safe_x == 6300 && car.safe_z == 4020);
    assert(car.safe_heading == 12420);
    assert(g_race.lap == 0 && g_race.best_lap == 0 && !saved && !messages);

    // Free-drive recovery must return to the last REAL road pose, even if
    // the player leaves the route grid or a different 32X branch is nearby.
    car.x = 7000 * 256;
    car.z = 8500 * 256;
    car_respawn(&car);
    assert(car.x == 6300 * 256 && car.z == 4020 * 256);
    assert(car.heading == 12420);
    s32 old_x = car.safe_x, old_z = car.safe_z;
    road_covered = 0;
    car.x = 6600 * 256;
    car.z = 4400 * 256;
    for (s32 i = 0; i < 16; i++) race_step(&car, 1);
    assert(car.safe_x == old_x && car.safe_z == old_z);
    assert(g_race.lap == 0 && g_race.cp_given == original_cp && !saved && !messages);
    road_covered = 1;
}
#endif

int main(int argc, char **argv)
{
    assert(argc == 2);
    r_init();
    memset(g_tune, TUNE_STOCK, sizeof(g_tune));
    tune_apply();
#ifdef WITH_VR_ORIGINAL_TRACKS
    if (!strcmp(argv[1], "original-branch-recovery")) {
        original_branch_recovery();
        puts("PASS: original-branch-recovery");
        return 0;
    }
#endif
    for (s32 set = 0; set < TRACK_SET_COUNT; set++) {
        s32 course = set * TRACKS_PER_SET;
#ifdef WITH_VRD_32X_TRACKS
        if (!strcmp(argv[1], "study-free-drive")) { study_free_drive(); break; }
        else
#endif
        if (!strcmp(argv[1], "wrong-way-motion")) wrong_way_motion(course);
        else if (!strcmp(argv[1], "wrong-way-cone")) wrong_way_cone(course);
        else if (!strcmp(argv[1], "wrong-way-projection-loss")) wrong_way_projection_loss(course);
        else if (!strcmp(argv[1], "finish-prior")) finish_prior(course);
        else if (!strcmp(argv[1], "finish-same")) finish_same(course);
        else if (!strcmp(argv[1], "finish-tie")) finish_tie(course);
        else if (!strcmp(argv[1], "finish-subunit")) finish_subunit(course);
        else if (!strcmp(argv[1], "respawn-back")) respawn_back(course);
        else if (!strcmp(argv[1], "respawn-cp")) respawn_cp(course);
        else if (!strcmp(argv[1], "respawn-lap")) respawn_lap(course);
        else if (!strcmp(argv[1], "respawn-forward")) respawn_forward(course);
        else if (!strcmp(argv[1], "respawn-grid")) respawn_grid(course);
        else if (!strcmp(argv[1], "jump-guard")) jump_guard(course);
        else assert(0 && "Unknown lifecycle regression case");
    }
    printf("PASS: %s\n", argv[1]);
    return 0;
}
