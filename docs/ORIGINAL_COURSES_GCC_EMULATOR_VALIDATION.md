# GNU ARM GBA runtime validation — imported original courses

November 22, 2026. The private user-owned US donor image was supplied to the
existing optional importer; source hashes and imported data were not committed
to GitHub. The base PR remains draft and stacked.

## Binary provenance

- GitHub source head before this documentation: `b2bd99032ff1c6cbd658979fcd5bdb541e8d0c61`.
- Donor image: exact 2,097,152-byte supported US ROM, SHA256
  `291e74f9b4d5a2c0f2b7cbf6f2e0fbf72c16cb6d83b1f58dda78209f9a64c486`.
  The original input's hash was checked again after building.
- Build: Debian Trixie GNU ARM EABI GCC 14.2.1 and binutils, using
  `make vr-original VR_ROM="/private/path/to/Virtua Racing (USA).md"`.
  A source-only GitHub workflow exports the cross-toolchain, but does not run
  with or contain the donor ROM.
- Current optional output: 1,597,688 bytes, SHA256
  `71d9b2fefccc9f3b5e5e3010f7d0a2f8107042fb37baee4f2c8130cfe0f2c8b2`.
- Static linked IWRAM: 27,536 of 32,512 bytes (data 14,112 + bss 13,424).
  Linked EWRAM: 229,544 of 262,144 bytes. No peak stack use is claimed.
- The existing custom-course GCC cartridge is available from the regular
  Build and source checks workflow (standard game, no donor assets).
- Private full test suite: **62 passed, no skips** with the verified donor
  image, including the renderer's signed-shift sanitizer regression.
  Source-only CI appropriately skips the private donor-dependent tests.

## Actual mGBA test protocol

An unmodified mGBA 0.10.x Linux core executes the GBA ARM CPU and its video
renderer. `tests/gba_emulator_smoke.c` samples buttons and reads the compiled
addresses of `g_frames` and `g_race`; it never writes gameplay RAM or injects
game logic. The test boots without the Nintendo BIOS logo (skip-BIOS setting),
presses Start, chooses Arcade, switches L/R to VR ORIGINALS, selects the
appropriate original course, then holds A to drive. Input holds were lengthened
to 12 emulated video frames for low-FPS course menus.

The smoke verifies selection of indices 3/4/5, forward movement after the
countdown, continuing screen updates and zero mGBA errors. Actual 240x160
framebuffers were captured for each original course's selection and driving
views. This is **not** a timed full lap or a three-lap Arcade completion.

A matched short rendering window was measured from each course-selection
capture to its driving capture, with exactly 817 emulated video frames
in every run. The emulator's display clock is approximately 59.7275 Hz.
Rendering FPS is `(draws_after - draws_before) / 817 * 59.7275`:

| Course | Previous Clang experimental | Previous GCC checkpoint | Current GNU GCC |
|---|---:|---:|---:|
| Original Big Forest | 11.99 | 15.13 | **14.69** |
| Original Bay Bridge | 13.09 | 17.40 | **16.67** |
| Original Acropolis | 11.33 | 13.67 | **13.16** |

Current exact counts: Big Forest 201 draws, Bay Bridge 228, Acropolis 180.
All three finish the test in the racing state with zero emulator errors.
Final race progress units: 4,341; -314; -114 respectively. Negative progress
on the latter courses represents being behind the start line on the grid,
not moving backward; each progressed from an initial more-negative start.
Pixels and route-selection maps were inspected in full native-resolution
screenshots. These measured numbers must not be conflated with earlier
full-race FPS measurements made under different controller protocols.

The current GCC build is clearly faster than the previously delivered Clang
experimental binary in the same smoke sequence. **It is also slightly slower
than the earlier GCC checkpoint.** Since both the compiler and source changed
across versions, no causal compiler-only or road-lookup speed claim follows.
Longer races, physical hardware, pit services and overpass/loop functionality
remain outside this particular smoke test.

## Automated standard-game CI

The source-only `.github/workflows/emulator-smoke.yml` builds the standard
homebrew GBA and runs the same real mGBA backend using ordinary input:
title, mode screen, course selection, then a throttle hold. Its artifacts
include native framebuffers and read-only progress logs. The portable mGBA
runtime workflow packages runtime libraries so users can run the exact
same test on their privately imported build without uploading the donor ROM.

The renderer's signed-negative fixed-point shifts were also repaired and
tested in `tests/render_clipping_host.c`, with explicit offscreen-left
polygon and near-plane crossing cases under signed-shift UBSan.

## Remaining goals

- Restore frame rate in crowded original-course Arcade views, especially
  current Acropolis scenes; avoid new heaps/framebuffers or unbounded arrays.
- Verify high-speed, braking, recovery and full three-lap progression on the
  **current** exact GCC optional binary, not only older validated revisions.
- Expand the custom track representation for elevation-aware roads, bridge
  crossings, non-planar loops and alternate racing routes.
- Continue native Mega Drive driving/steering experiments separately from
  the imported-course GBA game and retain strict original-hash verification.
