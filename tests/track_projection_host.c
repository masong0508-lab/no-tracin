// Compare the full elevation query with the height-free road projection.
// No original commercial data is stored in this test.
#include <assert.h>
#include <stdio.h>
#include "track.h"

#ifdef WITH_VR_ORIGINAL_TRACKS
static int height_queries;
s32 __real_vr_original_height(s32 course, s32 x, s32 z);
s32 __wrap_vr_original_height(s32 course, s32 x, s32 z)
{
    height_queries++;
    return __real_vr_original_height(course, x, z);
}
#endif

static void check_road(s32 course, s32 x, s32 z)
{
    TrackHit full = {0}, fast = {0};
    s32 found = track_find(x, z, &full);
#ifdef WITH_VR_ORIGINAL_TRACKS
    height_queries = 0;
#endif
    s32 found_fast = track_find_road(x, z, &fast);
    assert(found == found_fast);
    if (!found) return;
    assert(fast.h == 0);
    assert(fast.seg == full.seg && fast.along == full.along);
    assert(fast.lat == full.lat && fast.dist == full.dist);
#ifdef WITH_VR_ORIGINAL_TRACKS
    assert(height_queries == 0); // Fast query must not perform donor division.
    if (course >= TRACKS_PER_SET) {
        height_queries = 0;
        assert(track_find(x, z, &full));
        assert(height_queries == 1); // Full height contract remains intact.
    }
#endif
}

static void negative_elevation_geometry(void)
{
    // A below-sea-level road must be valid signed Q8, not negative shifts.
    static const TrackPt pts[2] = {
        {.x=4000, .z=4000, .y=-100, .uz=16384, .len=100},
        {.x=4000, .z=4100, .y=-50, .uz=16384, .len=100},
    };
    static u16 first[TRACK_GRID * TRACK_GRID];
    static u8 counts[TRACK_GRID * TRACK_GRID];
    static const u8 indexes[1] = {0};
    TrackDef synthetic = {0};
    synthetic.pts = pts;
    synthetic.count = 2;
    synthetic.lap = 200;
    synthetic.cell_first = first;
    synthetic.cell_count = counts;
    synthetic.cell_list = indexes;
    counts[(4050 >> TRACK_CELL_SHIFT) * TRACK_GRID + (4000 >> TRACK_CELL_SHIFT)] = 1;
    g_track = &synthetic;
    TrackHit h;
    assert(track_find(4000, 4050, &h));
    assert(h.seg == 0 && h.along == 50 && h.h == -75 * 256);
    assert(track_find_road(4000, 4050, &h));
    assert(h.seg == 0 && h.along == 50 && h.h == 0);
    g_track = 0;
}

int main(void)
{
    r_init();
    negative_elevation_geometry();
    int samples = 0;
    for (s32 course = 0; course < TRACK_COUNT; course++) {
        track_load(course);
        const TrackDef *t = g_track;
        assert(t && t->count > 0);
        s32 stride = t->count / 60;
        if (stride < 1) stride = 1;
        for (s32 i = 0; i < t->count; i += stride) {
            const TrackPt *p = &t->pts[i];
            check_road(course, p->x, p->z);
            samples++;
            s32 nx = p->uz / 4096, nz = -p->ux / 4096;
            check_road(course, p->x + nx, p->z + nz);
            check_road(course, p->x - nx, p->z - nz);
            samples += 2;
        }
        check_road(course, -1, -1);
        check_road(course, TRACK_WORLD, TRACK_WORLD);
        track_load(-1);
    }
    printf("PASS: %d real road samples across %d courses, exact projection fields\n", samples, TRACK_COUNT);
    return 0;
}
