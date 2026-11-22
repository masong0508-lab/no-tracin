// Circuit racing: the grid, CPU rivals that follow the racing line, laps,
// checkpoints and the arcade clock, the race HUD and the results.
#include "race.h"
#include "track.h"
#include "world.h"
#include "game.h"
#include "hud.h"
#include "menu.h"
#include "sound.h"
#include "softbody.h"

Race g_race;
// Race credit and road projection paired with the car's safe respawn pose.
static s32 safe_progress, safe_d;
// Consecutive observed Q8 poses resolve crossing time at the physical line.
static s32 previous_x, previous_z;

typedef struct {
    s32 s, v;                 // progress (Q8) and speed (Q8 per step)
    s32 lat, lat_target;      // lateral offset from the centreline
    s32 skill;                // Q8 share of the racing line's speed
    s32 hint, timer, color, cap;
    s32 x, y, z, heading, pitch, roll;   // pose for drawing and collisions
} Rival;

// Where the rivals sit across the road (offsets from the centreline).
// Imported courses can be narrower: every lane fits the narrowest segment.
static s32 race_half_width = TRACK_HALF_W;
static inline s32 lane_limit(void)
{
    return race_half_width > 30 ? race_half_width - 30 : 0;
}
static inline s32 lane_offset(s32 desired)
{
    s32 limit = lane_limit();
    return desired < limit ? desired : limit;
}
#define GRID_LAT   lane_offset(race_half_width * 9 / 20) // the two columns of the grid
#define PASS_LAT   lane_offset(race_half_width / 2 + 8)  // pulling out to pass
#define WANDER_LAT lane_offset(race_half_width / 2 - 4)  // drifting about the road
#define MAX_LAT    lane_limit()                         // shoved toward the edge
#define GRID_GAP   96                         // between grid slots, staggered left and right

static Rival rivals[RIVALS] EWRAM_BSS;
static u16 line_speed[256] EWRAM_BSS;       // CPU target speed at each point, Q8 per step
static u32 rng = 777;
static s32 rnd(s32 n) { rng = rng * 1103515245 + 12345; return (rng >> 16) % n; }
static s32 iabs(s32 v) { return v < 0 ? -v : v; }

static char *put(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *put_num(char *p, s32 v)
{
    char tmp[12];
    s32 i = 0;
    if (v < 0) { *p++ = '-'; v = -v; }
    do { tmp[i++] = '0' + v % 10; v /= 10; } while (v);
    while (i) *p++ = tmp[--i];
    *p = 0;
    return p;
}
// Physics steps in the arcade style: 1'02"35
static char *put_race_time(char *p, s32 steps)
{
    s32 secs = steps / 60, hund = (steps % 60) * 100 / 60;
    p = put_num(p, secs / 60);
    *p++ = '\'';
    *p++ = '0' + (secs % 60) / 10;
    *p++ = '0' + secs % 10;
    *p++ = '"';
    *p++ = '0' + hund / 10;
    *p++ = '0' + hund % 10;
    *p = 0;
    return p;
}

// ---------------------------------------------------------------- records

#define REC_MAGIC 0x3256544E      // "NTV2": the courses grew, older records don't count
// Keep each bank in the original three-course layout. Installing or removing
// the optional ROM courses must not resize or overwrite existing custom times.
typedef struct { s32 best_lap[TRACKS_PER_SET], best_race[TRACKS_PER_SET]; } RaceRecords;
static RaceRecords recs[TRACK_SET_COUNT];

static u32 records_magic(s32 set)
{
#ifdef WITH_VR_ORIGINAL_TRACKS
    if (set == TRACK_SET_ORIGINAL) return 0x31524F56 ^ g_vr_original_pack_version;
#else
    (void)set;
#endif
    return REC_MAGIC;
}

static u32 records_address(s32 set)
{
    return set == TRACK_SET_CUSTOM ? RACE_SAVE_AT : ORIGINAL_RACE_SAVE_AT;
}

static void race_records_save(s32 track)
{
    if (track < 0 || track >= TRACK_COUNT) return;
    s32 set = track / TRACKS_PER_SET;
    save_blob(records_magic(set), records_address(set), &recs[set], sizeof(recs[set]));
}

void race_records_load(void)
{
    for (s32 set = 0; set < TRACK_SET_COUNT; set++) {
        if (!load_blob(records_magic(set), records_address(set), &recs[set], sizeof(recs[set])))
            for (s32 i = 0; i < TRACKS_PER_SET; i++)
                recs[set].best_lap[i] = recs[set].best_race[i] = 0;
    }
}

s32 race_best_lap(s32 track)
{
    return track >= 0 && track < TRACK_COUNT ? recs[track / TRACKS_PER_SET].best_lap[track % TRACKS_PER_SET] : 0;
}
s32 race_best_time(s32 track)
{
    return track >= 0 && track < TRACK_COUNT ? recs[track / TRACKS_PER_SET].best_race[track % TRACKS_PER_SET] : 0;
}

// ---------------------------------------------------------------- setup

static void grid_slot(s32 slot, s32 *d, s32 *lat)
{
    *d = -(90 + slot * GRID_GAP);
    *lat = (slot & 1) ? GRID_LAT : -GRID_LAT;
}

// Corner speeds from the radius (what the tyres hold), then braking zones
// worked backward from each corner.
static void build_line_speed(void)
{
    const TrackDef *t = g_track;
    const s32 top = 3000, brake = 22;
    for (s32 i = 0; i < t->count; i++) {
        s32 v = isqrt(32 * 256 * t->pts[i].radius);
        line_speed[i] = v > top ? top : v;
    }
    for (s32 pass = 0; pass < 2; pass++)
        for (s32 i = t->count - 1; i >= 0; i--) {
            s32 j = i + 1 == t->count ? 0 : i + 1;
            s32 vj = line_speed[j];
            s32 lim = isqrt(vj * vj + 2 * brake * 256 * t->pts[i].len);
            if (lim < line_speed[i]) line_speed[i] = lim;
        }
}

static void rival_pose(Rival *r)
{
    track_point(r->s >> 8, r->lat, &r->x, &r->z, &r->y, &r->heading, &r->hint);
    r->pitch = track_pitch[r->hint];
    r->roll = 0;
#ifdef WITH_VR_ORIGINAL_TRACKS
    if (g_track && g_track >= g_tracks + TRACKS_PER_SET) {
        // Original roads can bank across the lane as well as climb along it.
        // Fit the plane beneath all four wheels: a tiny centre derivative
        // can cross a native height-cell boundary and misrepresent the bank.
        s32 s = isin(r->heading >> 6), c = icos(r->heading >> 6);
        s32 rx = (17 * c) >> 14, rz = (-17 * s) >> 14;
        s32 fx = (30 * s) >> 14, fz = (30 * c) >> 14;
        s32 heights[4];
        for (s32 i = 0; i < 4; i++) {
            s32 right = i & 1 ? 1 : -1, front = i & 2 ? -1 : 1;
            heights[i] = track_height(r->x + right * rx + front * fx,
                                      r->z + right * rz + front * fz);
        }
        s32 rise_r = heights[1] + heights[3] - heights[0] - heights[2];
        s32 rise_f = heights[0] + heights[1] - heights[2] - heights[3];
        s32 area = 4 * (rx * fz - rz * fx);
        // Use the actual integer sample positions, so coordinate rounding
        // cannot introduce a false slope on a planar stretch.
        s32 gx = (rise_r * fz - rise_f * rz) / area;
        s32 gz = (rise_f * rx - rise_r * fx) / area;
        if (gx >= -512 && gx <= 512 && gz >= -512 && gz <= 512) {
            s32 along = (gx * s + gz * c) >> 14;
            s32 across = (gx * c - gz * s) >> 14;
            r->y = (heights[0] + heights[1] + heights[2] + heights[3]) / 4;
            r->pitch = iatan2(along, 256);
            // car_matrix rolls before pitching: the longitudinal slope must
            // be included here to keep both body axes on the road plane.
            r->roll = iatan2(-across, isqrt(256 * 256 + along * along));
        }
    }
#endif
}

void race_begin(s32 track, s32 mode, Car *player)
{
    Race *g = &g_race;
    u8 *p = (u8 *)g;
    for (u32 i = 0; i < sizeof(*g); i++) p[i] = 0;
    const TrackDef *t = g_track;
    g->mode = mode;
    g->track = track;
    race_half_width = track_half_width(0);
    for (s32 i = 1; i < t->count; i++) {
        s32 width = track_half_width(i);
        if (width < race_half_width) race_half_width = width;
    }
    g->laps = mode == RACE_ARCADE ? t->laps : 9999;     // free run goes on until you quit
    g->time_left = t->start_time * 60;
    build_line_speed();
    rng = 777 + track * 31;

    s32 d, lat, x, z, y, h, hint = 0;
    s32 slot = mode == RACE_ARCADE ? RIVALS : 0;
    grid_slot(slot, &d, &lat);
    if (mode != RACE_ARCADE) lat = 0;
    track_point(d, lat, &x, &z, &y, &h, &hint);
    car_reset(player, x, z, h);
    g->progress = d;
    g->last_d = d + t->lap;
    g->hint = hint;
    g->position = slot + 1;
    safe_progress = g->progress;
    safe_d = g->last_d;
    previous_x = player->x;
    previous_z = player->z;

    for (s32 i = 0; i < RIVALS; i++) {
        Rival *r = &rivals[i];
        grid_slot(i, &d, &lat);
        r->s = d << 8;
        r->v = 0;
        r->lat = r->lat_target = lat;
        // The front of the grid is quicker; it spreads the field out.
        r->skill = 244 - i * 6 + (track % TRACKS_PER_SET) * 2 + rnd(4);
        r->hint = 0;
        r->timer = 60 + rnd(120);
        r->color = i;
        r->cap = 0x7FFF;
        rival_pose(r);
    }
}

void race_on_respawn(Car *player)
{
    if (!player || !g_track) return;
    // A known respawn returns to previously earned distance. Keep earned
    // laps/checkpoints and all clocks; repeating the road earns no bonus.
    g_race.progress = safe_progress;
    g_race.last_d = safe_d;
    g_race.wrong_way = g_race.wrong_steps = 0;
    previous_x = player->x;
    previous_z = player->z;
}

// ---------------------------------------------------------------- per step

// Physical traffic repeats each lap; standings and rubber banding do not.
// Most gaps are already local, so only lap-separated cars need a division.
static s32 traffic_gap(s32 gap)
{
    s32 lap = g_track->lap, half = lap / 2;
    if (gap > half || gap < -half) {
        gap %= lap;
        if (gap > half) gap -= lap;
        else if (gap < -half) gap += lap;
    }
    return gap;
}

static void rivals_step(Car *player, s32 started, s32 road_d, s32 plat)
{
    const TrackDef *t = g_track;
    s32 px = player->x >> 8, pz = player->z >> 8;
    s32 pspeed = car_speed(player);
    s32 tow = 0;
    for (s32 i = 0; i < RIVALS && g_race.mode == RACE_ARCADE; i++) {
        Rival *r = &rivals[i];
        if (started) {
            s32 seg = r->hint + 1 >= t->count ? 0 : r->hint + 1;
            s32 target = (line_speed[seg] * r->skill) >> 8;
            // Rubber band: ease off well ahead of the player, push when behind.
            // (The gaps are about time: on the big courses the cars go only a
            // little faster, so the distances grow less than the courses did.)
            s32 gap = (r->s >> 8) - g_race.progress;
            if (g_race.state == RS_RACING) {
                if (gap > 2400) target = (target * 184) >> 8;
                else if (gap > 650) target = (target * 212) >> 8;
                else if (gap < -2000) target = (target * 272) >> 8;
            }
            // Don't drive into the car ahead in the same lane: slow, then pull out.
            // (Checked every fourth step; the cap it sets holds in between.)
            if (((g_race.total_steps + i) & 3) == 0) r->cap = 0x7FFF;
            for (s32 k = -1; k < RIVALS && ((g_race.total_steps + i) & 3) == 0; k++) {
                if (k == i) continue;
                s32 os, olat, ov;
                if (k < 0) { os = road_d << 8; olat = plat; ov = pspeed; }
                else { os = rivals[k].s; olat = rivals[k].lat; ov = rivals[k].v; }
                s32 ahead = traffic_gap((os - r->s) >> 8);
                if (ahead <= 0 || ahead > 150 || iabs(olat - r->lat) > 56) continue;
                if (ov - 16 < r->cap) r->cap = ov - 16;
                if (r->timer > 20) r->timer = 20;
                r->lat_target = olat > 0 ? -PASS_LAT : PASS_LAT;
            }
            if (target > r->cap) target = r->cap;
            s32 dv = target - r->v;
            r->v += dv > 13 ? 13 : dv < -30 ? -30 : dv;
            if (r->v < 0) r->v = 0;
            r->s += r->v;
            if (--r->timer <= 0) {
                r->timer = 90 + rnd(200);
                r->lat_target = rnd(2 * WANDER_LAT + 1) - WANDER_LAT;
            }
            if (r->lat < r->lat_target) r->lat++;
            else if (r->lat > r->lat_target) r->lat--;
        }
        // Cars well away from the player only need their pose now and then.
        s32 lead = traffic_gap((r->s >> 8) - road_d);
        s32 far = iabs(lead) > r_far + 300;
        if (!far || ((g_race.total_steps + i) & 3) == 0 || !started) rival_pose(r);
        if (far) continue;

        // Slipstream: tucked in close behind a car at speed, the air drag drops.
        if (lead > 60 && lead < 450 && iabs(r->lat - plat) < 36 && pspeed > 1200) tow = 1;

        // Contact with the player.
        s32 dx = px - r->x, dz = pz - r->z;
        if (iabs(dx) > 70 || iabs(dz) > 70 || iabs((player->y - r->y) >> 8) > 40) continue;
        if (sb_valid(player)) {
            // Soft-body: the rival is three solid balls nose to tail that
            // push on the player's frame (and dent it where they hit).
            s32 fx = isin(r->heading >> 6), fz = icos(r->heading >> 6);
            s32 rvx = (r->v * fx) >> 14, rvz = (r->v * fz) >> 14;
            s32 hit = 0, shx = 0, shz = 0;
            for (s32 k = -1; k <= 1; k++) {
                s32 h = sb_push(r->x + ((fx * 28 * k) >> 14), (r->y >> 8) + 12, r->z + ((fz * 28 * k) >> 14),
                                22, rvx, rvz, &shx, &shz);
                if (h > hit) hit = h;
            }
            if (!hit) continue;
            if (hit > player->hit) player->hit = hit;
            if (hit > 120) sound_play(SFX_SCRAPE);
            s32 shove = 6;
            r->lat += r->lat > plat ? shove : -shove;
            if (r->lat > MAX_LAT) r->lat = MAX_LAT; else if (r->lat < -MAX_LAT) r->lat = -MAX_LAT;
            r->lat_target = r->lat;
            if (lead < 0) r->v = (r->v * 15) >> 4;
            else r->v += 40;
            continue;
        }
        s32 dist = isqrt(dx * dx + dz * dz);
        if (dist >= 54 || player->mode == CAR_CRASH) continue;
        if (!dist) { dx = 1; dist = 1; }
        s32 nx = (dx << 14) / dist, nz = (dz << 14) / dist;
        s32 pen = 54 - dist;
        player->x += (pen * nx) >> 6;
        player->z += (pen * nz) >> 6;
        s32 rvx = (r->v * isin(r->heading >> 6)) >> 14, rvz = (r->v * icos(r->heading >> 6)) >> 14;
        s32 rel = ((player->vx - rvx) * nx + (player->vz - rvz) * nz) >> 14;
        if (rel < 0) {
            player->vx -= (rel * 3 / 2 * nx) >> 14;
            player->vz -= (rel * 3 / 2 * nz) >> 14;
            player->hit = -rel;
            if (-rel > 120) sound_play(SFX_SCRAPE);
        }
        // A side hit shoves the rival across the road.
        s32 shove = pen > 8 ? 8 : pen;
        r->lat += r->lat > plat ? shove : -shove;
        if (r->lat > MAX_LAT) r->lat = MAX_LAT; else if (r->lat < -MAX_LAT) r->lat = -MAX_LAT;
        r->lat_target = r->lat;
        // Whoever was behind loses a little speed.
        if (lead < 0) r->v = (r->v * 15) >> 4;
        else r->v += 40;
    }
    if (tow && player->mode == CAR_GROUND) {
        s32 vx = player->vx >> 10, vz = player->vz >> 10;
        if (sb_valid(player)) sb_add_velocity(player, vx, vz);
        else { player->vx += vx; player->vz += vz; }
    }
    g_race.slipstream = tow;
}

void race_step(Car *player, s32 started)
{
    Race *g = &g_race;
    const TrackDef *t = g_track;
    RaceRecords *records = &recs[g->track / TRACKS_PER_SET];
    s32 record_index = g->track % TRACKS_PER_SET;
    s32 player_before = g->progress, rival_before[RIVALS];
    s32 x_before = previous_x, z_before = previous_z;
    previous_x = player->x;
    previous_z = player->z;
    if (g->mode == RACE_ARCADE && g->state == RS_RACING)
        for (s32 i = 0; i < RIVALS; i++) rival_before[i] = rivals[i].s;
    TrackHit hit;
    s32 x = player->x >> 8, z = player->z >> 8;
    s32 found = track_find(x, z, &hit);
    // Ignored projection jumps can offset credit from the actual location.
    // Traffic uses the current projection, or the last one if off the road.
    s32 d = found ? t->pts[hit.seg].dist + hit.along : g->last_d;
    rivals_step(player, started, d, found ? hit.lat : 0);
    if (found) {
        s32 delta = d - g->last_d;
        if (delta > t->lap / 2) delta -= t->lap;
        if (delta < -t->lap / 2) delta += t->lap;
        if (iabs(delta) < 200) g->progress += delta;   // reject unrelated projection jumps
        g->last_d = d;
        g->hint = hit.seg;
        // Respawn on the centreline here after a wreck.
        if (player->mode == CAR_GROUND && hit.dist < track_half_width(hit.seg) && (g->total_steps & 15) == 0) {
            s32 sx, sz, sy, sh, sint = hit.seg;
            track_point(d, 0, &sx, &sz, &sy, &sh, &sint);
            player->safe_x = sx;
            player->safe_z = sz;
            player->safe_heading = sh;
            safe_progress = g->progress;
            safe_d = d;
        }
        // Wrong way follows motion, including reverse gear, within a
        // roughly 120-degree backward cone relative to the road tangent.
        const TrackPt *p = &t->pts[hit.seg];
        s32 speed = car_speed(player);
        s32 along = (player->vx * p->ux + player->vz * p->uz) >> 14;
        if (speed > 300 && along * 2 < -speed && player->mode == CAR_GROUND) g->wrong_steps++;
        else g->wrong_steps = 0;
        g->wrong_way = g->wrong_steps > 45;
    }
    if (!started) return;

    s32 racing = g->state == RS_RACING;
    if (racing) {
        g->lap_steps++;
        g->total_steps++;
        if (g->extend_flash > 0) g->extend_flash--;
        if (g->mode == RACE_ARCADE) {
            if (g->time_left > 0) g->time_left--;
            if (g->time_left > 0 && g->time_left <= 600 && g->time_left % 60 == 0) sound_play(SFX_BEEP);
        }
        // Half-lap checkpoint.
        if (g->progress >= g->lap * t->lap + t->lap / 2 && g->cp_given <= g->lap && g->lap < g->laps) {
            g->cp_given = g->lap + 1;
            if (g->mode == RACE_ARCADE) {
                g->time_left += t->cp_time * 60;
                g->extend_flash = 120;
                game_message("CHECKPOINT", "EXTENDED TIME", PAL_GREEN, 120);
                sound_play(SFX_CHECKPOINT);
            }
        }
        // Lap line.
        if (g->progress >= (g->lap + 1) * t->lap) {
            if (g->lap < MAX_LAPS) g->lap_times[g->lap] = g->lap_steps;
            if (!g->best_lap || g->lap_steps < g->best_lap) g->best_lap = g->lap_steps;
            char a[24], b[24];
            s32 record = !records->best_lap[record_index] || g->lap_steps < records->best_lap[record_index];
            put_race_time(put(b, record ? "BEST LAP " : "LAP "), g->lap_steps);
            if (record) {
                records->best_lap[record_index] = g->lap_steps;
                g->new_best_lap = 1;
            }
            g->lap++;
            g->lap_steps = 0;
            if (g->lap >= g->laps) {
                g->state = RS_GOAL;
                g->end_timer = 0;
                if (g->mode == RACE_ARCADE &&
                    (!records->best_race[record_index] || g->total_steps < records->best_race[record_index])) {
                    records->best_race[record_index] = g->total_steps;
                    g->new_best_race = 1;
                }
                game_message("GOAL!", b, PAL_YELLOW, 600);
                sound_play(SFX_BEST);
            } else {
                if (g->mode == RACE_ARCADE) {
                    g->time_left += t->cp_time * 60;
                    g->extend_flash = 120;
                }
                if (g->lap == g->laps - 1 && g->mode == RACE_ARCADE) put(a, "FINAL LAP");
                else put_num(put(a, "LAP "), g->lap + 1);
                game_message(a, b, record ? PAL_GREEN : PAL_WHITE, 150);
                sound_play(record ? SFX_BEST : SFX_LAP);
            }
            if (record || g->state == RS_GOAL)
                race_records_save(g->track);
        }
        // A checkpoint or lap reached on the last clock tick still counts.
        // Resolve those crossings first, then end only an unfinished race
        // whose clock remains empty after any earned time extension.
        if (g->mode == RACE_ARCADE && g->state == RS_RACING && g->time_left == 0) {
            g->state = RS_TIMEOVER;
            g->end_timer = 0;
            game_message("TIME OVER", "", PAL_RED, 600);
            sound_play(SFX_FAIL);
        }
    } else {
        g->end_timer++;
    }

    // Running order.
    s32 pos = 1;
    for (s32 i = 0; i < RIVALS && g->mode == RACE_ARCADE; i++)
        if ((rivals[i].s >> 8) > g->progress) pos++;
    // At the flag, classify by crossing time, not distance driven after it.
    if (racing && g->state == RS_GOAL && g->mode == RACE_ARCADE) {
        s32 goal = g->laps * t->lap, goal_q8 = goal * 256;
        long long player_need = goal - player_before;
        long long player_move = g->progress - player_before;
        // Credit is integral and can jump at a route's closing seam. Use
        // the actual Q8 movement across the start point's perpendicular
        // plane when available, retaining all dot-product precision.
        // The credit rules above still decide whether this lap is earned.
        const TrackPt *line = &t->pts[0];
        s32 lx = line->x * 256, lz = line->z * 256;
        long long side_before = (long long)(x_before - lx) * line->ux +
                                (long long)(z_before - lz) * line->uz;
        long long side_after = (long long)(previous_x - lx) * line->ux +
                               (long long)(previous_z - lz) * line->uz;
        if (side_before <= 0 && side_after >= 0 && side_after > side_before) {
            player_need = -side_before;
            player_move = side_after - side_before;
        }
        pos = 1;
        for (s32 i = 0; i < RIVALS; i++) {
            s32 before = rival_before[i], after = rivals[i].s;
            if (before >= goal_q8) pos++;
            else if (after >= goal_q8 && player_need > 0 && player_move > 0) {
                // Compare fractions of this step without division. Exact
                // ties favor the player, as with the strict running order.
                if ((long long)(goal_q8 - before) * player_move <
                    (long long)player_need * (after - before)) pos++;
            }
        }
    }
    // Include movement on the goal tick, then freeze the finishing place.
    if (racing) g->position = pos;
}

s32 race_done(void)
{
    return g_race.state != RS_RACING && g_race.end_timer > 240;
}

// ---------------------------------------------------------------- drawing

void race_draw_rivals(void)
{
    if (g_race.mode != RACE_ARCADE) return;
    // Only the nearest gets the detailed car; the rest are simple shapes.
    s32 depth[RIVALS], near0 = -1;
    for (s32 i = 0; i < RIVALS; i++) {
        const Rival *r = &rivals[i];
        s32 side;
        depth[i] = r_visible(r->x, r->z, 60) ? r_depth(r->x, r->z, &side) : -1;
        if (depth[i] < 0 || depth[i] > r_far) { depth[i] = -1; continue; }
        if (near0 < 0 || depth[i] < depth[near0]) near0 = i;
    }
    for (s32 i = 0; i < RIVALS; i++) {
        if (depth[i] < 0) continue;
        const Rival *r = &rivals[i];
        s32 d = i == near0 ? depth[i] : depth[i] < 320 ? 320 : depth[i];
        car_draw_rival(r->x, r->y >> 8, r->z, r->heading, r->pitch, r->roll, r->color, d);
    }
}

static const char *const ordinals[8] = { "ST", "ND", "RD", "TH", "TH", "TH", "TH", "TH" };

void race_hud(const Car *car, s32 frame)
{
    const Race *g = &g_race;
    char buf[24], *p;
    if (g->mode == RACE_ARCADE) {
        hud_text_centered(3, "TIME", 0, PAL_YELLOW);
        s32 secs = (g->time_left + 59) / 60;
        put_num(buf, secs);
        s32 pal = g->extend_flash ? PAL_GREEN : secs <= 10 && (frame & 32) ? PAL_RED : PAL_WHITE;
        hud_text_huge(16, buf, pal);
        hud_text(6, 3, "POS", 0, PAL_CYAN);
        p = put_num(buf, g->position);
        put(p, ordinals[g->position - 1]);
        hud_text(6, 12, buf, 1, PAL_CYAN);
        p = put(buf, "/");
        put_num(p, RIVALS + 1);
        hud_text(6, 29, buf, 0, PAL_CYAN);
        if (g->slipstream && (frame & 8)) hud_text(6, 40, "SLIPSTREAM", 0, PAL_GREEN);
    } else {
        hud_text(6, 3, "FREE RUN", 0, PAL_YELLOW);
    }
    s32 lap = g->lap + 1 > g->laps ? g->laps : g->lap + 1;
    p = put(buf, "LAP ");
    p = put_num(p, lap);
    if (g->mode == RACE_ARCADE) { p = put(p, "/"); put_num(p, g->laps); }
    hud_text_right(236, 3, buf, 0, PAL_YELLOW);
    put_race_time(buf, g->state == RS_RACING ? g->lap_steps : g->lap_times[g->lap ? g->lap - 1 : 0]);
    hud_text_right(236, 13, buf, 0, PAL_WHITE);
    s32 best = g->best_lap ? g->best_lap : race_best_lap(g->track);
    if (best) {
        p = put(buf, "BEST ");
        put_race_time(p, best);
        hud_text_right(236, 23, buf, 0, PAL_CYAN);
    }

    // Speed and revs, bottom right.
    s32 mph = (car_speed(car) * 67) / 2560;
    put_num(buf, g_opt[OPT_UNITS] ? mph * 16 / 10 : mph);
    hud_text_right(212, 140, buf, 1, PAL_YELLOW);
    hud_text(216, 146, g_opt[OPT_UNITS] ? "KMH" : "MPH", 0, PAL_YELLOW);
    hud_tach(160, 130, car->rpm);
    if (g->wrong_way && (frame & 16)) hud_text_centered(98, "WRONG WAY", 1, PAL_RED);
}

// ---------------------------------------------------------------- results

void race_results(void)
{
    const Race *g = &g_race;
    static char left[12][16] EWRAM_BSS, right[12][16] EWRAM_BSS;
    const char *lp[12], *rp[12];
    u8 hi[12];
    s32 n = 0;
    for (s32 i = 0; i < 12; i++) { lp[i] = left[i]; rp[i] = right[i]; hi[i] = 0; }
    put(left[n], "COURSE");
    put(right[n++], g_tracks[g->track].name);
#ifdef WITH_VR_ORIGINAL_TRACKS
    put(left[n], "COURSE SET");
    put(right[n++], track_set_name(g->track / TRACKS_PER_SET));
#endif
    if (g->mode == RACE_ARCADE) {
        put(left[n], "POSITION");
        if (g->state == RS_GOAL) { char *p = put_num(right[n], g->position); put(p, ordinals[g->position - 1]); }
        else put(right[n], "--");
        hi[n++] = g->state == RS_GOAL && g->position == 1;
        put(left[n], "TOTAL TIME");
        if (g->state == RS_GOAL) put_race_time(right[n], g->total_steps);
        else put(right[n], "--");
        hi[n++] = g->new_best_race;
    }
    for (s32 i = 0; i < g->lap && i < MAX_LAPS && n < 10; i++) {
        put_num(put(left[n], "LAP "), i + 1);
        put_race_time(right[n], g->lap_times[i]);
        hi[n++] = g->lap_times[i] == g->best_lap;
    }
    put(left[n], "COURSE RECORD");
    if (race_best_lap(g->track)) put_race_time(right[n], race_best_lap(g->track));
    else put(right[n], "--");
    hi[n++] = g->new_best_lap;
    const char *title = g->state == RS_GOAL ? (g->mode == RACE_ARCADE && g->position == 1 ? "WINNER!" : "GOAL") :
                        g->state == RS_TIMEOVER ? "GAME OVER" : "RESULTS";
    menu_freeze();
    menu_table(title, lp, rp, hi, n, "A CONTINUE");
}
