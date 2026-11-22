// Synthetic SRAM fixture: no source ROM or imported track data is needed.
// Including race.c lets this test exercise its private persistence entry point
// while menu.c's actual checksumming and byte-wise SRAM routines are linked.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "race.c"

volatile u8 test_sram[0x8000];
#ifdef WITH_VR_ORIGINAL_TRACKS
const u32 g_vr_original_pack_version = 0x11223344;
#endif

int main(void)
{
    u8 *sram = (u8 *)test_sram;
    memset(sram, 0xA5, sizeof(test_sram));

    // This is the layout of a pre-pack save, independently of RaceRecords.
    struct LegacyRecords { s32 laps[3], races[3]; };
    const struct LegacyRecords legacy = {{101, 202, 303}, {401, 502, 603}};
    assert(sizeof(legacy) == 24);
    save_blob(0x3256544E, 0x7C00, &legacy, sizeof(legacy));

    u8 before[sizeof(test_sram)];
    memcpy(before, sram, sizeof(before));
    race_records_load();
    for (s32 i = 0; i < 3; i++) {
        assert(race_best_lap(i) == legacy.laps[i]);
        assert(race_best_time(i) == legacy.races[i]);
    }
    assert(race_best_lap(-1) == 0 && race_best_time(TRACK_COUNT) == 0);
    race_records_save(-1);
    race_records_save(TRACK_COUNT);
    assert(memcmp(before, sram, sizeof(before)) == 0);

#ifdef WITH_VR_ORIGINAL_TRACKS
    assert(TRACK_COUNT == 6);
    for (s32 i = 3; i < 6; i++) {
        assert(race_best_lap(i) == 0 && race_best_time(i) == 0);
    }
    recs[1].best_lap[1] = 7654;
    recs[1].best_race[1] = 23456;
    race_records_save(4);
    // Only the optional 24-byte payload and its 8-byte wrapper may change.
    // This also protects the city map, custom records and tuning blocks.
    assert(memcmp(sram, before, 0x7C40) == 0);
    assert(memcmp(sram + 0x7C60, before + 0x7C60, sizeof(before) - 0x7C60) == 0);
    memset(recs, 0, sizeof(recs));
    race_records_load();
    assert(race_best_lap(4) == 7654 && race_best_time(4) == 23456);
    for (s32 i = 0; i < 3; i++) assert(race_best_lap(i) == legacy.laps[i]);
    u8 originals[32];
    memcpy(originals, sram + 0x7C40, sizeof(originals));
#else
    assert(TRACK_COUNT == 3);
#endif

    recs[0].best_lap[2] = 304;
    race_records_save(2);
    struct LegacyRecords readback;
    assert(load_blob(0x3256544E, 0x7C00, &readback, sizeof(readback)));
    assert(readback.laps[0] == 101 && readback.laps[1] == 202 && readback.laps[2] == 304);
    assert(memcmp(readback.races, legacy.races, sizeof(legacy.races)) == 0);

#ifdef WITH_VR_ORIGINAL_TRACKS
    assert(memcmp(originals, sram + 0x7C40, sizeof(originals)) == 0);
    sram[0x7C44] ^= 0x80; // corrupt just the imported record payload
    race_records_load();
    assert(race_best_lap(2) == 304 && race_best_lap(4) == 0);

    recs[1].best_lap[0] = 987;
    race_records_save(3);
    sram[0x7C40] ^= 1;    // a different import revision has a different identity
    race_records_load();
    assert(race_best_lap(3) == 0 && race_best_lap(2) == 304);
    puts("PASS: optional records preserve legacy saves, isolate banks and reject corruption/revisions");
#else
    assert(memcmp(sram + 0x7C20, before + 0x7C20, sizeof(before) - 0x7C20) == 0);
    memset(recs, 0, sizeof(recs));
    race_records_load();
    assert(race_best_lap(2) == 304 && race_best_time(2) == 603);
    puts("PASS: stock build preserves legacy schema and leaves optional save space untouched");
#endif
    return 0;
}
