// Stunt scoring with combos, and the Stunt Park lap: checkpoints, lap
// times, a best lap and its ghost car.
#ifndef GAME_H
#define GAME_H

#include "car.h"

typedef struct {
    s32 score;
    s32 combo;              // multiplier while combo_timer runs
    s32 combo_timer;
    s32 drift_steps, drift_points;

    char msg1[24], msg2[24];  // big centre message and the line under it
    s32 msg_timer, msg_pal;

    s32 lap_active, lap_steps, next_cp;
    s32 best_steps, last_steps;
    s32 cp_flash;           // steps left showing a checkpoint split
    s32 nitro;              // 0..NITRO_MAX, refilled by stunts
} Game;

extern Game g_game;

// Career records, kept in the save.
typedef struct {
    s32 best_steps, high_score, stunts, loops, crashes;
    s32 best_jump, best_air, top_mph, play_secs;
    u32 stars;              // one bit per star collected
    s32 flips;              // tricks landed
} Records;
extern Records g_rec;
extern s32 g_new_best;                  // set when a lap beats the best (for autosave)
extern s32 g_units_kmh;                 // speeds in messages
#define NITRO_MAX  1000
#define STAR_COUNT 22
extern s32 cheat_nitro;
s32  game_stars(void);                  // stars collected
void game_draw_stars(s32 frame);        // star sprites
void *game_ghost_data(s32 **count, s32 *max_bytes);   // best-lap ghost, for saving

void game_reset(void);
void game_step(Car *c);                 // once per physics step, after car_step
void game_draw_world(void);             // checkpoint gates
s32  game_ghost(Car *ghost);            // fills in the ghost car; 0 when none
s32  game_arrow(s32 x, s32 z);          // world angle (1024 units) to the next gate, -1 if none
void game_message(const char *a, const char *b, s32 pal, s32 steps);
void game_time_text(char *buf, s32 steps);   // physics steps as m:ss.t

#endif
