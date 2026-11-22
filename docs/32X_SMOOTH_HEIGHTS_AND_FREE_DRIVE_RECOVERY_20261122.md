# Five 32X road studies — smoothed driving and real-road recovery

Checkpoint: November 22, 2026. The latest source belongs to the still-draft
stacked PR #2. This iteration does **not** add official Deluxe races, original
Sega geometry meshes, AI routes, or a source-exact Race Drivin' physics engine.

## Ground height and Newton contact

The ROM-verified 32X BSP road surface is sampled on a **32-world-unit**
raster for the optional privately generated GBA road studies. Previously
`vrd32x_study_height()` used the nearest sample on that raster, producing
one-step height jumps across long inclines, even while rendering a continuous
road strip. The new **Q8 bilinear height interpolation** uses at most four
neighboring samples within a fully supported source road patch.

A source-mask hole or unsupported/outside-road neighbor **never becomes a
bridge**: the code retains the last verified sample instead of interpolating
across it. Surface classification still uses the original discrete mask.
Exact 32-unit grid vertices retain their old heights. No heap, static
large arrays, or framebuffer allocations were added.

The soft-body and car ground-plane path uses the **same interpolated Q8
surface** for height and central gradient. All four footprint probes must
still be in supported road. Synthetic positive and negative grade cases
verify exact Q8 values, slopes and no interpolation through a missing
neighbor under signed arithmetic sanitization.

Road rendering now selects a small grade-dependent asphalt palette shade
using **the four elevation values already fetched** for the existing
64-unit rendering quads. Neither the 180-road-face budget nor the 25
environment ground quads were increased.

## Genuine Free Drive recovery, not fictional races

The five 32X slots use approximated skeleton *staging* routes, not verified
Sega start/finish lines, race directions, branch links, or checkpoints.
The old shared race code could:
- Display WRONG WAY according to a guessed skeleton direction
- Award internal fictional laps on that skeleton
- Return a recovering vehicle to the staging route rather than the last
  actual supported road segment, losing exploration progress on a side road

The optional-study `race_step()` path now clears wrong-way and rivals,
updates only **approximate non-scoring navigation progress**, never gives
lap/finish awards, and records a safe pose every 16 completed physics steps
when the car is grounded on an **actual BSP-supported road mask**. The
original two three-course sets continue to use the unmodified competitive
race-step path, preserving their lap and checkpoint systems. The recovery
regression places a car far off the staging spline on a simulated verified
side road and checks recovery position/heading, no warning and no fake
scoring. It also checks off-road driving cannot overwrite that saved pose.

## Exact executable and test results

Source revision: `9feeacd3a71e342dde6e9defff7b3b16b52e0f8f`.

| ROM | SHA-256 | Size |
|---|---|---:|
| Custom-only | `86ca1a2b8e7805f6aaf800a585f9ab3ff4f286eadbfc9d7b034a9b0770b1e2a1` | 347,980 B |
| Custom plus original Genesis VR | `53a6043409617863eec3f1497d8e654680ba435d89303a27ee9b835e6b2772d0` | 1,599,352 B |
| All eleven environments / private 32X studies | `bb1f56ec094aa74f38a69c87caf161137e91b18236f46aaaa665c557204f6735` | 2,145,508 B |

The first two are **byte-for-byte identical** to their previous delivered
variants when rebuilt from the same archived source with GNU ARM GCC 14.2.1.
This change is isolated to the optional study configuration. The private
source ROMs were verified unchanged, with full SHA-256s of their supported US
Genesis, US 32X Deluxe and Race Drivin' original images.

Static link budget: **IWRAM 27,568 / 32,512 bytes** (14,112 data +
13,456 BSS); **EWRAM 229,544 / 262,144 bytes**. These are static regions
only; maximum stack use and physical-console behavior are not measured.

Full local test suite using all three private verified donor ROMs:
**101/101 passed, no skips**. The public GitHub Actions run discovers
101 tests and passes **81 tests with 20 expected donor-dependent skips**,
plus standard GCC build and real mGBA controller smoke. Source-only CI also
uploads a compiled mGBA test probe for user-local ROM validation.

Private identical-controller input comparison: 817 emulated video frames per
study slot from course preview to driving capture, about 59.7275 video
frames/second, no guest RAM writes, zero emulator errors:

| Study slot | Earlier ROM FPS | Updated ROM FPS | Change |
|---|---:|---:|---:|
| 0 | 19.739 | 19.885 | +0.146 |
| 1 | 19.885 | 19.885 | 0.000 |
| 2 | 25.148 | 24.856 | -0.292 |
| 3 | 22.736 | 22.297 | -0.439 |
| 4 | 22.005 | 22.297 | +0.292 |

The worst observed regression was **0.439 FPS (~1.9%)** in this short
controller stream. These are **rendered frames per emulated video second**,
not native 60 FPS, measured host throughput, a whole-race average, or
a physical-hardware guarantee. Captures/JSON input-result logs are
packaged privately for verification.

A further **10/10 mGBA runs passed**: every 32X study slot with independently
selected **Newton Motion** and either **Arcade** or **Race Style Handling**.
The live and persisted option bytes were confirmed by read-only emulator
inspection; all ten rendered and advanced without reported errors.

## Rebuild, controls, and remaining limits

For this user-owned private version only, supply the two verified original
source ROMs to:

```sh
make vr-deluxe-study VR_ROM="/path/to/Virtua Racing (USA).md" \
  VR32X_ROM="/path/to/Virtua Racing Deluxe 32X (U).bin"
```

In No Tracin': Start → Free Run or Arcade → press L/R twice to
**32X ROAD STUDIES** → choose SLOT 0–4 → A. The studies force **FREE
DRIVE** and cannot earn race records. A accelerates; B brakes; D-pad
steers. Options → HANDLING and MOTION PHYS select independent modes.

**Still unverified or unfinished:** named Highland and Sand Park identity,
original 32X start positions, true connected pit/branch/route graph,
proper native vehicle models and scenery, source-exact vertical scaling,
multi-minute exploration and whole official races, original Genesis
Race Drivin' gravity routine parity, and GBA physical-hardware timing.
No user-owned original ROMs or generated commercial asset headers
are committed to GitHub.
