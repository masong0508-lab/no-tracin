// Races on the Virtua-style circuits: an arcade race against seven CPU cars
// with a time limit that checkpoints extend, or a free run to set lap times.
#ifndef RACE_H
#define RACE_H

#include "car.h"

enum { RACE_ARCADE, RACE_FREE };
enum { RS_RACING, RS_GOAL, RS_TIMEOVER };

#define RIVALS     7
#define MAX_LAPS   8

typedef struct {
    s32 mode, track, laps;
    s32 progress;             // distance driven past the start line (negative on the grid)
    s32 last_d, hint;
    s32 lap;                  // laps completed
    s32 lap_steps, total_steps;
    s32 lap_times[MAX_LAPS];
    s32 best_lap;             // fastest lap this race
    s32 time_left;            // physics steps on the clock (arcade)
    s32 cp_given;             // half-lap checkpoints passed
    s32 position;             // 1 = leading
    s32 state, end_timer;
    s32 extend_flash, wrong_way, wrong_steps;
    s32 new_best_lap, new_best_race;
    s32 slipstream;           // tucked in behind a rival this step
} Race;

extern Race g_race;

void race_begin(s32 track, s32 mode, Car *player);
void race_step(Car *player, s32 started);     // once per physics step
void race_draw_rivals(void);
void race_hud(const Car *car, s32 frame);
s32  race_done(void);                         // the race is over and the results are due
void race_results(void);                      // results screen; returns on A

// Records for each circuit, kept in the battery save.
s32  race_best_lap(s32 track);
s32  race_best_time(s32 track);
void race_records_load(void);

#endif
