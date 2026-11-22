// The fixed-step scheduling body is extracted verbatim from src/main.c.
// Actual car_step, game_step, race_step and soft body run beneath it.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "car.h"
#include "softbody.h"
#include "game.h"
#include "race.h"
#include "track.h"
#include "world.h"
#include "tune.h"
#include "menu.h"
#include "sound.h"
#include "hud.h"

#include "game_loop_settings.inc"
const TrackDef *g_track;
static const TrackPt points[] = {{.x=1000,.z=6000,.uz=16384,.len=10000,.radius=8000}};
#define COURSE {.pts=points,.count=1,.lap=10000,.laps=3,.cp_time=32,.start_time=60}
const TrackDef g_tracks[TRACK_COUNT] = {COURSE,COURSE,COURSE};
s16 track_pitch[256];
const Loop the_loop = {1000,-10000,200,100,100};
u8 g_opt[OPT_COUNT];
static s32 next_distance, wall_z, on_pad, step_count, boost_steps, countdown;
static s32 verbose, finished_steps, crash_steps;
static u32 clock_now, last, acc;

s32 world_height(s32 x, s32 z) { (void)x; (void)z; return 0; }
s32 world_surface(s32 x, s32 z) { (void)x; (void)z; return SURF_ROAD; }
s32 world_in_water(s32 x, s32 z) { (void)x; (void)z; return 0; }
s32 world_collide(s32 x, s32 z, s32 radius, s32 *nx, s32 *nz)
{
    (void)x; *nx = *nz = 0;
    if (wall_z && z + radius > wall_z) { *nz = -16384; return z + radius - wall_z; }
    return 0;
}
s32 world_ground_plane(s32 x,s32 z,s32 r,s32 *h,s32 *gx,s32 *gz)
{ (void)x;(void)z;(void)r;*h=*gx=*gz=0;return 1; }
s32 track_find(s32 x,s32 z,TrackHit *hit)
{ (void)x;(void)z; *hit=(TrackHit){.along=next_distance}; return 1; }
s32 track_height_near(s32 segment,s32 x,s32 z)
{ (void)segment;(void)x;(void)z;return 0; }
void track_gradient(s32 x,s32 z,s32 *gx,s32 *gz)
{ (void)x;(void)z;*gx=*gz=0; }
s32 track_half_width(s32 segment) { (void)segment;return 150; }
void track_point(s32 d,s32 lat,s32 *x,s32 *z,s32 *y,s32 *h,s32 *hint)
{ (void)d;(void)lat;*x=10000;*z=10000;*y=*h=*hint=0; }
void loop_point(s32 theta,s32 *x,s32 *y,s32 *z) { (void)theta;*x=*y=*z=0; }
void sound_play(s32 sound) { (void)sound; }
void save_blob(u32 magic,u32 at,const void *data,u32 size)
{ (void)magic;(void)at;(void)data;(void)size; }
static void props_step(Car *c) { (void)c; }
static void fx_step(Car *c) { (void)c; }
void sound_step(const Car *c) { (void)c; }
static void edit_step(Car *c) { (void)c; }
static s32 edit_boosting(void) { return on_pad; }
static void camera_step(s32 *cs,Car *c,s32 view) { (void)cs;(void)c;(void)view; }
static u32 cycles(void) { return clock_now; }

static void observed_car_step(Car *car,u16 keys)
{
    if (verbose) printf("STEP n=%d keys=%u boost=%d fuel=%d mode=%d countdown=%d state=%d vz=%d\n",step_count,keys,car->boost,g_game.nitro,car->mode,countdown,g_race.state,car->vz);
    step_count++;
    boost_steps += car->boost != 0;
    if (g_race.state != RS_RACING) finished_steps++;
    if (car->mode == CAR_CRASH && car->boost) crash_steps++;
    car_step(car,keys);
}

static void run_frame(Car *car,u16 keys,u32 elapsed)
{
    s32 cs=0,view=0;
    clock_now += elapsed;
    #define car_step observed_car_step
    #include "main_loop_body.inc"
    #undef car_step
}

static Car setup(const char *scenario, s32 model)
{
    memset(g_opt,0,sizeof(g_opt));
    memset(g_tune,10,sizeof(g_tune));
    car_g=35;car_grip=38;car_power=28000;car_crashes=1;car_softbody=model;
    cheat_fly=cheat_hop=cheat_glitch=cheat_nitro=0;
    tune_apply();game_reset();
    memset(&g_race,0,sizeof(g_race));
    g_race.mode=RACE_FREE;g_race.laps=9999;g_race.time_left=3600;
    g_track=&g_tracks[0];
    Car car;
    car_reset(&car,1000,6000,0);
    if (!strcmp(scenario,"fuel")) g_game.nitro=6;
    if (!strcmp(scenario,"countdown")) countdown=1;
    if (!strcmp(scenario,"crash")) {
        car_softbody=0;car.vz=3500;wall_z=6050;
    }
    if (!strcmp(scenario,"recover")) {
        car.mode=CAR_CRASH;car.timer=150;
    }
    if (!strcmp(scenario,"goal")) {
        g_race.laps=3;g_race.lap=2;g_race.cp_given=3;
        g_race.progress=29995;g_race.last_d=9995;next_distance=0;
    }
    if (!strcmp(scenario,"timeover")) {
        g_race.mode=RACE_ARCADE;g_race.laps=3;g_race.time_left=1;
    }
    return car;
}

int main(int argc,char **argv)
{
    if(argc<3)return 2;
    r_init();setvbuf(stdout,NULL,_IOLBF,0);
    Car car=setup(argv[1], !strcmp(argv[1], "cadence") || argc < 4 ? 1 : atoi(argv[3]));
    int grouping=atoi(argv[2]);
    if(!strcmp(argv[1],"cadence")) {
        assert(argc>=4);int frames=atoi(argv[3]);
        if (argc > 4) g_opt[OPT_SPEED] = atoi(argv[4]);
        if (argc > 5 && atoi(argv[5])) {
            g_opt[OPT_SLOWMO] = 1;
            car_softbody = 0; car.mode = CAR_AIR; car.y = 1000 * 256; car_g = 0;
        }
        if (argc > 6 && atoi(argv[6])) clock_now = last = 0xffffffffu - STEP_CYCLES;
        u32 length = step_len[g_opt[OPT_SPEED]] * (g_opt[OPT_SLOWMO] ? 3 : 1);
        u32 elapsed=length * grouping / 100;
        for(int f=0;f<frames;f++) run_frame(&car,0,elapsed);
        printf("CADENCE frame_percent=%d frames=%d expected_steps=%llu actual_steps=%d remainder=%u elapsed=%llu\n",grouping,frames,(unsigned long long)elapsed*frames/length,step_count,acc,(unsigned long long)elapsed*frames);
        return 0;
    }
    verbose=1;
    u16 keys=KEY_A|KEY_R|KEY_LEFT;
    for(int n=0;n<4;n+=grouping) run_frame(&car,keys,STEP_CYCLES*grouping);
    const s32 *car_words=(const s32 *)&car, *race_words=(const s32 *)&g_race;
    printf("CAR"); for (u32 i=0;i<sizeof(car)/sizeof(s32);i++) printf(" %d",car_words[i]); puts("");
    printf("RACE"); for (u32 i=0;i<sizeof(g_race)/sizeof(s32);i++) printf(" %d",race_words[i]); puts("");
    printf("RESULT scenario=%s group=%d steps=%d boost_steps=%d fuel=%d vz=%d x=%d z=%d steer=%d state=%d countdown=%d boost_during_crash=%d\n",argv[1],grouping,step_count,boost_steps,g_game.nitro,car.vz,car.x,car.z,car.steer,g_race.state,countdown,crash_steps);
}
