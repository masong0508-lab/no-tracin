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
#define KEY_START 8
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
    if (argc != 5) {
        fprintf(stderr, "usage: %s game.gba g_frames_hex g_race_hex screenshots_dir\n", argv[0]);
        return 2;
    }
    uint32_t frame_addr = strtoul(argv[2], NULL, 16);
    uint32_t race_addr = strtoul(argv[3], NULL, 16);
    if (!frame_addr || !race_addr) return 2;
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

    run(4, KEY_START); run(60, 0);
    shot(argv[4], "mode"); status("mode", frame_addr, race_addr);
    run(4, KEY_A); run(80, 0);
    shot(argv[4], "course"); status("course", frame_addr, race_addr);
    run(4, KEY_A); run(45, 0);

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
