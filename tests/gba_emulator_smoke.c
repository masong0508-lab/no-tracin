#define _GNU_SOURCE 1
/* Real mGBA ARM core smoke test: buttons only; no test-state injection. */
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba-util/vfs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#define KEY_A 1
#define KEY_B 2
#define KEY_UP 64
#define KEY_DOWN 128
#define KEY_START 8
#define KEY_RIGHT 16
#define KEY_L 512
#define NPIXELS (240 * 160)
static struct mCore *core;
static color_t screen[NPIXELS];
static int errors;

static void on_log(struct mLogger *l, int category, enum mLogLevel level,
                   const char *fmt, va_list args)
{
    (void)l; (void)category;
    if (level & (mLOG_FATAL | mLOG_ERROR)) {
        errors++; vfprintf(stderr, fmt, args); fputc('\n', stderr);
    }
}

static void run(unsigned frames, unsigned keys)
{
    core->setKeys(core, keys);
    for (unsigned i = 0; i < frames; ++i) core->runFrame(core);
}

static uint32_t read32(uint32_t address) { return core->busRead32(core, address); }
static unsigned read8(uint32_t address) { return core->busRead8(core, address); }
static void tap(unsigned key) { run(12, key); run(30, 0); }

static unsigned long long pixel_hash(void)
{
    unsigned long long h = 1469598103934665603ull;
    for (int i = 0; i < NPIXELS; i++)
        h = (h ^ screen[i]) * 1099511628211ull;
    return h;
}

static void shot(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(3); }
    fprintf(f, "P6\n240 160\n255\n");
    for (int i = 0; i < NPIXELS; i++) {
        const unsigned char rgb[3] = {
            screen[i] & 255, (screen[i] >> 8) & 255, (screen[i] >> 16) & 255
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void status(const char *phase, uint32_t frame_addr, uint32_t race_addr)
{
    printf("{\"phase\":\"%s\",\"emulated_frames\":%u,\"draws\":%u,"
           "\"track\":%d,\"progress\":%d,\"race_state\":%d,"
           "\"framebuffer_hash\":\"%016llx\",\"errors\":%d}\n",
           phase, core->frameCounter(core), read32(frame_addr),
           (int32_t)read32(race_addr + 4), (int32_t)read32(race_addr + 12),
           (int32_t)read32(race_addr + 84), pixel_hash(), errors);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    if (argc != 5 && argc != 7 && argc != 10 && argc != 12) {
        fprintf(stderr, "usage: %s game.gba g_frames_hex g_race_hex screenshots_dir\n", argv[0]);
        return 2;
    }
    uint32_t frame_addr = strtoul(argv[2], NULL, 16);
    uint32_t race_addr = strtoul(argv[3], NULL, 16);
    if (!frame_addr || !race_addr) return 2;
    int original = argc >= 7 ? atoi(argv[5]) : 0;
    int course_index = argc >= 7 ? atoi(argv[6]) : 0;
    int physics = argc >= 10 ? atoi(argv[7]) : -1;
    uint32_t options_addr = argc >= 10 ? strtoul(argv[8], NULL, 16) : 0;
    uint32_t model_addr = argc >= 10 ? strtoul(argv[9], NULL, 16) : 0;
    int motion = argc == 12 ? atoi(argv[10]) : -1;
    uint32_t motion_addr = argc == 12 ? strtoul(argv[11], NULL, 16) : 0;
    if (physics >= 0 && (physics >= 4 || !options_addr || !model_addr)) return 2;
    if (motion >= 0 && (motion >= 2 || !options_addr || !motion_addr)) return 2;
    // The optional Deluxe study set has FIVE slots after the two old
    // three-course sets. Preserve 0/1 command-line modes for older builds.
    if (original < 0 || original > 2 || course_index < 0 ||
        course_index >= (original == 2 ? 5 : 3)) return 2;
    struct mLogger logger = {.log = on_log, .filter = NULL};
    mLogSetDefaultLogger(&logger);
    core = mCoreFind(argv[1]);
    if (!core || !core->init(core)) return 2;
    mCoreInitConfig(core, NULL);
    mCoreConfigSetDefaultIntValue(&core->config, "skipBios", 1);
    mCoreConfigSetDefaultIntValue(&core->config, "useBios", 0);
    mCoreConfigSetDefaultIntValue(&core->config, "idleOptimization", 0);
    core->loadConfig(core, &core->config);
    core->setVideoBuffer(core, screen, 240);
    core->setAudioBufferSize(core, 1024);
    if (!mCoreLoadFile(core, argv[1])) return 2;
    core->reset(core);

    run(180, 0);
    unsigned title_draws = read32(frame_addr);
    shot(argv[4], "title");
    status("title", frame_addr, race_addr);
    if (title_draws < 10) { fprintf(stderr, "title did not draw\n"); return 1; }

    run(12, KEY_START); run(60, 0);
    shot(argv[4], "mode"); status("mode", frame_addr, race_addr);
    if (physics >= 0 || motion >= 0) {
        // Title -> Arcade mode menu -> Options at index 5. Test physical
        // settings through real buttons, without writing guest RAM.
        for (int i = 0; i < 5; ++i) tap(KEY_DOWN);
        tap(KEY_A); run(60, 0);
        if (physics >= 0) {
            tap(KEY_DOWN); // First option: Handling.
            for (int i = 0; i < physics; ++i) tap(KEY_RIGHT);
            shot(argv[4], "handling_options");
            unsigned selected = read8(options_addr + 14);
            int live = (int32_t)read32(model_addr);
            if (selected != (unsigned)physics || live != physics) {
                fprintf(stderr, "Handling mismatch: expected %d got %u/%d\n", physics, selected, live);
                return 1;
            }
            printf("PASS: independent handling profile %d\n", physics);
        }
        if (motion >= 0) {
            tap(KEY_DOWN); // Second option: Motion Physics.
            if (physics < 0) tap(KEY_DOWN);
            for (int i = 0; i < motion; ++i) tap(KEY_RIGHT);
            shot(argv[4], "motion_options");
            unsigned selected = read8(options_addr + 15);
            int live = (int32_t)read32(motion_addr);
            if (selected != (unsigned)motion || live != motion) {
                fprintf(stderr, "Motion mismatch: expected %d got %u/%d\n", motion, selected, live);
                return 1;
            }
            printf("PASS: independent motion physics %d\n", motion);
        }
        tap(KEY_B); run(40, 0);
        if (motion >= 0) {
            // NTR2 SysSave: magic (4 bytes), options 0..15, unchanged size.
            unsigned saved = read8(0x0E000013);
            if (saved != (unsigned)motion) {
                fprintf(stderr, "SRAM motion byte expected %d got %u\n", motion, saved);
                return 1;
            }
            printf("PASS: independent motion saved in reserved SRAM option[15]\n");
        }
        for (int i = 0; i < 5; ++i) tap(KEY_UP);
    }
    run(12, KEY_A); run(80, 0);
    for (int set = 0; set < original; ++set) tap(KEY_L);
    for (int i = 0; i < course_index; i++) { run(12, KEY_RIGHT); run(30, 0); }
    shot(argv[4], "course"); status("course", frame_addr, race_addr);
    run(12, KEY_A); run(45, 0);
    int expected_track = (original == 2 ? 6 : original * 3) + course_index;
    if ((int32_t)read32(race_addr + 4) != expected_track) {
        fprintf(stderr, "Wrong selected track: expected %d, got %d\n",
                expected_track, (int32_t)read32(race_addr + 4));
        return 1;
    }

    if (physics >= 0 && (int32_t)read32(model_addr) != physics) {
        fprintf(stderr, "Course loading changed handling mode\n");
        return 1;
    }
    if (motion >= 0 && (int32_t)read32(motion_addr) != motion) {
        fprintf(stderr, "Course loading changed motion physics\n");
        return 1;
    }
    unsigned before_draw = read32(frame_addr);
    int32_t before_progress = (int32_t)read32(race_addr + 12);
    run(760, KEY_A);
    unsigned after_draw = read32(frame_addr);
    shot(argv[4], "driving"); status("driving", frame_addr, race_addr);

    if (after_draw <= before_draw + 20 || errors) {
        fprintf(stderr, "draw stall or emulator error: %u->%u, errors %d\n",
                before_draw, after_draw, errors);
        return 1;
    }
    if ((int32_t)read32(race_addr + 12) <= before_progress) {
        fprintf(stderr, "no forward race progress: %d->%d\n",
                before_progress, (int32_t)read32(race_addr + 12));
        return 1;
    }
    puts("PASS: title, mode, course selection, racing, real framebuffer");
    core->deinit(core);
    return 0;
}
