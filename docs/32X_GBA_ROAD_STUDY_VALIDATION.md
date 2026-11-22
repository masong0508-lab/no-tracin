# Five drivable 32X road studies on GBA — private source-ROM import

Checkpoint: November 22, 2026. Source branch: `improve/original-virtua-courses-20261121`.

## Delivered and deliberately limited

The **private optional** GNU ARM EABI GBA build contains **11 selectable environments**: three No Tracin' custom circuits, three previously imported Genesis/SVP Virtua Racing circuits, and five new **32X ROAD STUDIES** labeled `SLOT 0` through `SLOT 4`. Change sets in Arcade/Free Run with L/R. The five new environments are **always FREE DRIVE**, not Arcade races. All tracks retain their own original selection behavior; the game's saved lap/record blobs remain unmodified.

Road-study geometry comes from the *verified* US Virtua Racing Deluxe 32X cartridge (SHA-256 `cc2917895842ba42ce871a07a327c5467a5b5df8b7999ccc4b7b5bea9d4caf6b`) using the tested read-only BSP boundary and height-plane parser. The separate importer hashes the exact cartridge before any output, samples its five supported source-data streams on a 512×512 grid, then packs non-background 8×8 sample tiles into ROM-resident `u16` heights and masks. Each terrain sample is 32 game world units wide, and the collision height queries use those samples. The renderer submits a bounded set of nearby 64-world-unit road polygons; no additional framebuffer or heap allocation.

**The labels are intentionally anonymous**: the 32X source track number to named Highland/Sand Park/etc. and exact Sega start-line/route mapping have **not** been verified. Each staging loop is an automatically skeletonized road-mask path for safe spawning, proximity, and camera placement; it is **not** the game's authentic racing line, pit service, branching navigation, or validated lap timing. The converted height scale `(source_height − slot_minimum) / 32 + 12` is also experimental and does not establish Sega's physical vertical units. Scenery and car models are No Tracin' stock graphics rather than Sega's original models. The current saved physics options still permit Soft-Body, Arcade, Hard Style, Race Style Handling and Original/Newton Motion, independently.

## Building and privacy

The **normal `make`** and **original three-course `make vr-original`** do not need the 32X source, Python scientific libraries, or private generated arrays. The new importer is deliberately gated behind:

```sh
python3 -m pip install -r tools/requirements-vrd32x-study.txt
make vr-deluxe-study VR_ROM='/path/to/Virtua Racing (USA).md' \
  VR32X_ROM='/path/to/Virtua Racing Deluxe 32X (U).bin'
```

Generated source-ROM-derived `vrd32x_study_data.h` and `vrd32x_study_tracks.h` remain in the ignored `build/vr-deluxe-study/` directory, as do all build objects and the combined GBA binary. **Never commit a commercial source ROM or generated imported assets to GitHub**. Public sources contain the importer and formulas only.

## Measured tests

- The previous full private regression suite passed **98/98** against the supplied verified source ROMs. Two extra source-only tests for a **fully synthetic** 8×8 C terrain fixture now pass: exact heights, road/offroad classification, Q8 slope normals, out-of-range safety, study set isolation, and importer hash/version gating.
- GCC built the 347,980-byte standard GBA, 1,599,352-byte Genesis original-course GBA, and **2,145,068-byte** three-set 32X study GBA without warnings. IWRAM `.data` 14,112 + `.bss` 13,456 = **27,568 / 32,512 bytes**; EWRAM static **229,544 / 262,144 bytes**. These sizes do **not** account for worst-case stack use.
- Fresh mGBA controller-input smoke tests **all passed** for study slots 0–4 (race IDs 6–10), with correct set selection, rendering progress and zero emulator errors, and actual 240×160 framebuffers. No guest RAM was modified. Test-only controller interposition supplied a second L press to navigate the third course-set menu; the game ROM was not patched.
- Short rendering windows of 817 emulated NTSC frames produced about **19.7–25.1 rendered frames per second**, depending on slot; values are *not* long-run/worst-case game performance. One slot's final view showed 0 mph after the longer throttle hold, so sustained travel/playability and reliable navigation remain to be improved. Smoke tests do not establish complete circuits or all-road accessibility.
- Source-only CI and standard mGBA should verify no regression after publication. The private 32X-derived assets are not part of GitHub CI.

## Remaining milestones — not represented as finished

1. Trace authentic 32X track identities, starts/checkpoints, branches and routes against actual 32X gameplay before enabling competitive 32X Arcade modes.
2. Decode original 32X car and object meshes, palette/texture data, scene details, rendering order and prop placement, respecting GBA RAM and FPS.
3. Verify 3D height scale, banked-road transitions, jumps and collision on all five slots using native physical measurements.
4. Run complete laps and sustained high-speed steering/recovery on actual GBA hardware or a real emulator with varied controls.
5. Trace the actual Race Drivin' Genesis 68000 gravity/tyre and suspension routines via execution (not opcode statistics) before any claim of porting authentic Atari/Race Drivin' physics.

## Additional independent Newton × Handling emulator validation

After the five default-motion study smokes, a separate real mGBA pass selected
**Motion Physics: Newton** through the options menu, alongside **Handling:
Arcade** and then **Handling: Race Style**, on each of the five new road-study
slots. All **10 of 10** runs passed. For each run the emulator confirmed
handling byte 14 and motion byte 15 in the existing SRAM/save option record,
then entered the correct 32X study ID 6–10, advanced race-progress readouts
relative to the staging path and continued rendering with zero emulator errors.
The test changed actual controller states only, without altering guest state.
This is a 760-frame smoke test, not validation of the original Sega racing
routes or a full-length handling/physics stability benchmark.
