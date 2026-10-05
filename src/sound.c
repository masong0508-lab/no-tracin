// DMG sound: everything here is register pokes, updated once per physics step.
#include "sound.h"

#define REG_SND1SWEEP (*(volatile u16 *)0x04000060)
#define REG_SND1CNT   (*(volatile u16 *)0x04000062)
#define REG_SND1FREQ  (*(volatile u16 *)0x04000064)
#define REG_SND2CNT   (*(volatile u16 *)0x04000068)
#define REG_SND2FREQ  (*(volatile u16 *)0x0400006C)
#define REG_SND3SEL   (*(volatile u16 *)0x04000070)
#define REG_SND3CNT   (*(volatile u16 *)0x04000072)
#define REG_SND3FREQ  (*(volatile u16 *)0x04000074)
#define REG_SND4CNT   (*(volatile u16 *)0x04000078)
#define REG_SND4FREQ  (*(volatile u16 *)0x0400007C)
#define REG_SNDDMGCNT (*(volatile u16 *)0x04000080)
#define REG_SNDDSCNT  (*(volatile u16 *)0x04000082)
#define REG_SNDSTAT   (*(volatile u16 *)0x04000084)
#define WAVE_RAM      ((volatile u16 *)0x04000090)

#define RESTART 0x8000
#define ENV(vol, up, step) (((vol) << 12) | ((up) << 11) | ((step) << 8))
#define DUTY_12 0x0000
#define DUTY_25 0x0040
#define DUTY_50 0x0080

// Square-channel frequency values for C4 (index 0) up to C7 (36).
static const u16 notes[37] = {
    1547, 1575, 1602, 1627, 1650, 1673, 1694, 1714, 1732, 1750, 1767, 1783,
    1798, 1812, 1825, 1837, 1849, 1860, 1871, 1881, 1890, 1899, 1907, 1915,
    1923, 1930, 1936, 1943, 1949, 1954, 1959, 1964, 1969, 1974, 1978, 1982, 1985,
};
enum { C4 = 1, D4 = 3, E4 = 5, F4 = 6, G4 = 8, A4 = 10, B4 = 12,
       C5 = 13, D5 = 15, E5 = 17, F5 = 18, G5 = 20, A5 = 22, B5 = 24,
       C6 = 25, D6 = 27, E6 = 29, F6 = 30, G6 = 32, A6 = 34, C7 = 37 };

// Jingles: pairs of (note + 1 or 0 for a rest, steps), ending with 0, 0.
static const u8 j_stunt[] = { C5, 5, E5, 5, G5, 5, C6, 14, 0, 0 };
static const u8 j_loop[]  = { G4, 4, C5, 4, E5, 4, G5, 4, C6, 4, E6, 18, 0, 0 };
static const u8 j_check[] = { E6, 6, 0, 2, E6, 12, 0, 0 };
static const u8 j_lap[]   = { C5, 5, G5, 5, C6, 5, E6, 5, G6, 22, 0, 0 };
static const u8 j_best[]  = { C5, 6, E5, 6, G5, 6, C6, 10, G5, 5, C6, 26, 0, 0 };
static const u8 j_combo[] = { A5, 3, C6, 3, E6, 8, 0, 0 };
static const u8 j_start[] = { C6, 6, G6, 16, 0, 0 };
static const u8 j_beep[]  = { A5, 10, 0, 0 };
static const u8 j_go[]    = { A6, 28, 0, 0 };
static const u8 j_fail[]  = { E5, 6, C5, 6, A4, 18, 0, 0 };

static const u8 *jingle;
static s32 jingle_left;
static s32 noise_left, noise_prio, wind_on, overtone_on, squeal_vol;
static s32 engine_vol, engine_on, prev_mode, lope;
static u32 seed = 99;
static s32 mode_no_engine;

void sound_mode(s32 mode)
{
    REG_SNDDMGCNT = mode == 2 ? 0xFF00 : 0xFF77;
    mode_no_engine = mode != 0;
    if (mode_no_engine && !jingle) { REG_SND1CNT = 0; REG_SND1FREQ = RESTART; overtone_on = 0; }
}

static s32 rnd(s32 n)
{
    seed = seed * 1103515245 + 12345;
    return (seed >> 16) % n;
}

void sound_init(void)
{
    REG_SNDSTAT = 0x0080;          // master enable first
    REG_SNDDMGCNT = 0xFF77;        // all four channels, both sides, full volume
    REG_SNDDSCNT = 0x0002;         // DMG mix at 100%
    REG_SND1SWEEP = 0x0008;        // no sweep

    // Engine waveform: two uneven sawtooth pulses per cycle for a lumpy V8 growl.
    static const u8 wave[32] = {
        15, 14, 12, 10, 9, 8, 7, 6, 5, 5, 4, 4, 3, 3, 2, 2,
        11, 10, 9, 8, 7, 6, 5, 5, 4, 3, 3, 2, 2, 1, 1, 0,
    };
    REG_SND3SEL = 0x0040;          // play bank 1 so the CPU can fill bank 0
    for (s32 i = 0; i < 8; i++) {
        const u8 *w = &wave[i * 4];
        WAVE_RAM[i] = ((w[0] << 4) | w[1]) | (((w[2] << 4) | w[3]) << 8);
    }
    REG_SND3SEL = 0x0000;          // 32 samples from bank 0, channel off for now
    sound_silence();
}

void sound_silence(void)
{
    REG_SND3SEL = 0x0000;
    REG_SND2CNT = 0;
    REG_SND2FREQ = RESTART;
    if (!jingle) { REG_SND1CNT = 0; REG_SND1FREQ = RESTART; }
    engine_on = 0;
    overtone_on = 0;
    squeal_vol = 0;
}

static void noise(s32 prio, s32 steps, u32 env, u32 freq)
{
    if (noise_left > 0 && prio < noise_prio) return;
    noise_prio = prio;
    noise_left = steps;
    wind_on = 0;
    REG_SND4CNT = env;
    REG_SND4FREQ = freq | RESTART;
}

void sound_play(s32 sfx)
{
    const u8 *j = 0;
    switch (sfx) {
    case SFX_CRASH:    noise(4, 110, ENV(15, 0, 7), (6 << 4) | 5); return;
    case SFX_SPLASH:   noise(4, 90, ENV(14, 0, 6), (2 << 4) | 3); return;
    case SFX_BIG_LAND: noise(3, 36, ENV(15, 0, 3), (6 << 4) | 4); return;
    case SFX_LAND:     noise(2, 24, ENV(12, 0, 2), (7 << 4) | 6); return;
    case SFX_SCRAPE:   noise(1, 10, ENV(11, 0, 1), (3 << 4) | 8 | 1); return;
    case SFX_STUNT:      j = j_stunt; break;
    case SFX_LOOP:       j = j_loop;  break;
    case SFX_CHECKPOINT: j = j_check; break;
    case SFX_LAP:        j = j_lap;   break;
    case SFX_BEST:       j = j_best;  break;
    case SFX_COMBO:      j = j_combo; break;
    case SFX_START:      j = j_start; break;
    case SFX_BEEP:       j = j_beep;  break;
    case SFX_GO:         j = j_go;    break;
    case SFX_FAIL:       j = j_fail;  break;
    default: return;
    }
    jingle = j;
    jingle_left = 0;
    overtone_on = 0;
}

static void step_jingle(void)
{
    if (!jingle || --jingle_left > 0) return;
    if (jingle[0] == 0 && jingle[1] == 0) {        // finished
        jingle = 0;
        REG_SND1CNT = 0;
        REG_SND1FREQ = RESTART;
        return;
    }
    s32 note = jingle[0];
    jingle_left = jingle[1];
    jingle += 2;
    if (note) {
        REG_SND1CNT = DUTY_50 | ENV(12, 0, 3);
        REG_SND1FREQ = notes[note - 1] | RESTART;
    } else {
        REG_SND1CNT = 0;
        REG_SND1FREQ = RESTART;
    }
}

void sound_step(const Car *c)
{
    step_jingle();
    if (noise_left > 0) noise_left--;

    // Impacts from the car's own telemetry.
    if (c->mode == CAR_CRASH && prev_mode != CAR_CRASH)
        sound_play(c->crash_kind == EV_SPLASH ? SFX_SPLASH : SFX_CRASH);
    else if (c->landed > 1000) sound_play(SFX_BIG_LAND);
    else if (c->landed > 300)  sound_play(SFX_LAND);
    if (c->hit > 260) sound_play(SFX_SCRAPE);
    prev_mode = c->mode;

    // Engine: pitch follows the revs; louder on the throttle.
    if (c->mode == CAR_CRASH || mode_no_engine) {
        if (engine_on) { REG_SND3SEL = 0; engine_on = 0; }
    } else {
        s32 rpm = c->rpm < 600 ? 600 : c->rpm;
        s32 n = 2048 - 983040 / rpm + rnd(5) - 2;
        if (rpm < 1300 && (++lope & 15) < 3) n -= 10;   // idle lope
        if (n > 2000) n = 2000;
        s32 vol = c->shift_timer > 4 ? 0x6000 :              // 25% while the clutch is in
                  c->throttle ? 0x2000 :                     // 100%
                  rpm < 1300 ? 0x4000 : 0x8000;              // idle 50%, coasting 75%
        if (!engine_on) {
            REG_SND3SEL = 0x0080;
            REG_SND3CNT = vol;
            REG_SND3FREQ = n | RESTART;
            engine_on = 1;
            engine_vol = vol;
        } else {
            if (vol != engine_vol) { REG_SND3CNT = vol; engine_vol = vol; }
            REG_SND3FREQ = n;
        }
        // A thin pulse an octave up gives the engine its rasp.
        if (!jingle) {
            if (!overtone_on) {
                REG_SND1CNT = DUTY_12 | ENV(3, 0, 0);
                REG_SND1FREQ = (n > 2040 ? 2040 : n) | RESTART;
                overtone_on = 1;
            } else {
                REG_SND1FREQ = n;
            }
        }
    }

    // Tyres: a wobbling squeal while sliding or spinning up.
    s32 speed = car_speed(c), want = 0;
    if (c->mode == CAR_GROUND) {
        if (c->skid && speed > 400) want = speed > 1500 ? 10 : 8;
        else if (c->spin) want = 7;
    }
    if (want != squeal_vol) {
        REG_SND2CNT = want ? (DUTY_50 | ENV(want, 0, 0)) : 0;
        REG_SND2FREQ = 1938 | RESTART;
        squeal_vol = want;
    } else if (want) {
        REG_SND2FREQ = 1932 + rnd(14) + (speed >> 9);
    }

    // Wind while flying fast; gravel rumble on the grass.
    s32 windy = (c->mode == CAR_AIR && speed > 900) || c->boost;
    if (noise_left <= 0) {
        if (windy && !wind_on) {
            REG_SND4CNT = ENV(4, 0, 0);
            REG_SND4FREQ = (4 << 4) | 2 | RESTART;
            wind_on = 1;
        } else if (!windy && wind_on) {
            REG_SND4CNT = 0;
            REG_SND4FREQ = RESTART;
            wind_on = 0;
        }
    }
}
