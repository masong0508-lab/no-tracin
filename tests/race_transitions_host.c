// Synthetic route positions exercise the production race state machine,
// including exactly simultaneous clock expiration and checkpoint/lap events.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "race.c"

static const TrackPt points[] = {{.len = 1000, .radius = 8000}};
#define COURSE {.pts = points, .count = 1, .lap = 1000, .laps = 3, .cp_time = 10}
const TrackDef g_tracks[TRACK_COUNT] = {
    COURSE, COURSE, COURSE,
#ifdef WITH_VR_ORIGINAL_TRACKS
    COURSE, COURSE, COURSE,
#endif
};
const TrackDef *g_track;
s16 track_pitch[256];
#ifdef WITH_VR_ORIGINAL_TRACKS
const u32 g_vr_original_pack_version = 0x11223344;
#endif
static s32 next_distance, sounds[SFX_FAIL + 1], saved;
static char last_message[32];

s32 track_find(s32 x, s32 z, TrackHit *hit)
{
    (void)x; (void)z;
    *hit = (TrackHit){.along = next_distance};
    return 1;
}
s32 track_half_width(s32 segment) { (void)segment; return 150; }
s32 track_height(s32 x, s32 z) { (void)x; (void)z; return 0; }
void track_point(s32 d, s32 lat, s32 *x, s32 *z, s32 *y, s32 *heading, s32 *hint)
{
    (void)d; (void)lat;
    *x = *z = 10000; *y = *heading = *hint = 0;
}
s32 car_speed(const Car *car) { (void)car; return 0; }
s32 sb_valid(const Car *car) { (void)car; return 0; }
void sb_add_velocity(Car *car, s32 vx, s32 vz)
{
    (void)car; (void)vx; (void)vz;
    assert(0 && "Non-softbody transition fixture cannot receive a frame impulse");
}
s32 sb_push(s32 x, s32 y, s32 z, s32 radius, s32 vx, s32 vz, s32 *px, s32 *pz)
{
    (void)x; (void)y; (void)z; (void)radius; (void)vx; (void)vz; (void)px; (void)pz;
    assert(0 && "Distant fixture rivals must not contact the player");
    return 0;
}
void sound_play(s32 sound)
{
    assert(sound >= 0 && sound <= SFX_FAIL);
    sounds[sound]++;
}
void game_message(const char *a, const char *b, s32 palette, s32 steps)
{
    (void)b; (void)palette; (void)steps;
    assert(strlen(a) < sizeof(last_message));
    strcpy(last_message, a);
}
void save_blob(u32 magic, u32 at, const void *data, u32 size)
{
    (void)magic; (void)data;
    assert(at == RACE_SAVE_AT || at == ORIGINAL_RACE_SAVE_AT);
    assert(size == 24);
    saved++;
}

static Car reset_case(s32 course, s32 progress, s32 clock)
{
    memset(&g_race, 0, sizeof(g_race));
    memset(recs, 0, sizeof(recs));
    memset(rivals, 0, sizeof(rivals));
    memset(sounds, 0, sizeof(sounds));
    last_message[0] = 0;
    saved = 0;
    g_track = &g_tracks[course];
    g_race.mode = RACE_ARCADE;
    g_race.track = course;
    g_race.laps = 3;
    g_race.progress = progress;
    g_race.last_d = progress % 1000;
    g_race.lap = progress / 1000;
    g_race.cp_given = progress / 1000 + (progress % 1000 >= 500);
    g_race.time_left = clock;
    g_race.lap_steps = 100;
    g_race.total_steps = 2345;
    g_race.position = 8;
    for (s32 i = 0; i < RIVALS; i++) {
        rivals[i].s = 10000 * 256;
        rivals[i].timer = 100;
        rivals[i].cap = 0x7fff;
    }
    return (Car){0};
}

int main(void)
{
    r_init();
    for (s32 set = 0; set < TRACK_SET_COUNT; set++) {
        s32 course = set * TRACKS_PER_SET;
        Car car = reset_case(course, 499, 1);
        next_distance = 500;
        race_step(&car, 1);
        assert(g_race.state == RS_RACING && g_race.time_left == 600);
        assert(g_race.cp_given == 1 && sounds[SFX_CHECKPOINT] == 1 && sounds[SFX_FAIL] == 0);
        assert(strcmp(last_message, "CHECKPOINT") == 0);
        next_distance = 501;
        race_step(&car, 1);
        assert(g_race.time_left == 599 && sounds[SFX_CHECKPOINT] == 1);

        car = reset_case(course, 999, 1);
        next_distance = 0;
        race_step(&car, 1);
        assert(g_race.state == RS_RACING && g_race.time_left == 600 && g_race.lap == 1);
        assert(g_race.lap_times[0] == 101 && sounds[SFX_FAIL] == 0 && saved == 1);
        assert(strcmp(last_message, "LAP 2") == 0);

        car = reset_case(course, 2999, 1);
        next_distance = 0;
        race_step(&car, 1);
        assert(g_race.state == RS_GOAL && g_race.time_left == 0 && g_race.lap == 3);
        assert(g_race.total_steps == 2346 && sounds[SFX_FAIL] == 0 && saved == 1);
        assert(strcmp(last_message, "GOAL!") == 0);
        assert(race_best_time(course) == 2346 && race_best_lap(course) == 101);

        // Move from second to first on the actual goal tick. Rival motion
        // during the post-race coast must not subsequently change the result.
        car = reset_case(course, 2999, 100);
        g_race.position = 2;
        for (s32 i = 0; i < RIVALS; i++) rivals[i].s = 2000 * 256;
        rivals[0].s = 2999 * 256 + 128;
        next_distance = 0;
        race_step(&car, 1);
        assert(g_race.state == RS_GOAL && g_race.progress == 3000 && g_race.position == 1);
        rivals[0].s = 3002 * 256;
        next_distance = 1;
        race_step(&car, 1);
        assert(g_race.progress == 3001 && g_race.position == 1 && g_race.end_timer == 1);

        car = reset_case(course, 100, 1);
        next_distance = 101;
        race_step(&car, 1);
        assert(g_race.state == RS_TIMEOVER && g_race.time_left == 0 && g_race.end_timer == 0);
        assert(sounds[SFX_FAIL] == 1 && saved == 0 && strcmp(last_message, "TIME OVER") == 0);
        next_distance = 500;
        race_step(&car, 1);
        assert(g_race.state == RS_TIMEOVER && g_race.time_left == 0 && g_race.end_timer == 1);
        assert(sounds[SFX_FAIL] == 1 && sounds[SFX_CHECKPOINT] == 0);

        car = reset_case(course, 499, 2);
        next_distance = 500;
        race_step(&car, 1);
        assert(g_race.state == RS_RACING && g_race.time_left == 601);

        car = reset_case(course, 100, 0);
        g_race.mode = RACE_FREE;
        next_distance = 101;
        race_step(&car, 1);
        assert(g_race.state == RS_RACING && g_race.time_left == 0 && sounds[SFX_FAIL] == 0);
    }
    puts("PASS: last-tick checkpoints/laps count, goal-tick rank freezes, and unearned timeouts still end races");
    return 0;
}
