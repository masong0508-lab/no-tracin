# ROM-faithful course maps: source-surface previews, not guessed road-study loops

Checkpoint: 2026-11-22. Stacked on the still-draft optional-course PR #2.
This change improves *map geometry fidelity* without inventing named 32X
courses, source-absent meshes, route checkpoints, or Sega assets. The user's
private donor ROM and extracted headers never enter Git history.

## Original Mega Drive/SVP courses — real road masks

The source 68000 decoder already imports **Big Forest, Bay Bridge, Acropolis**
polygon sections, original main route, surface collision mask and height
planes from the exact verified US cartridge. But the course-select map
preview still drew the main route as a uniform two-pixel line, omitting
asphalt-covered pit lanes and side branches and showing incorrect widths.

The private Genesis importer now scans each original source **two-bit
asphalt mask** to compute four GBA-world display bounds, with a small
framing margin. `VrOriginalCourse` stores four **ROM-constant u16 bounds**.
These are not new extracted public files; they are written only to the
ignored private output header, and the manifest records the bounds for
audit. Existing source polygon and collision data are unchanged.

`vr_original_map` now draws the actual source asphalt-area silhouette at
course-selection resolution, including original side roads/branches and
source road widths. It queries the same packed source surface as collision,
rather than the centreline; a few subpixel probes keep narrow source lanes
visible at downsampled preview sizes. The start mark and dynamic car dot
still derive from the verified original main-route nodes. A synthetic
host-side regression verifies two disjoint original-mask patches appear,
then verifies the map disappears if all source asphalt cells become solid.
It also checks safe marker rendering. Custom course maps are untouched.

This **does not** complete pit-branch *race progression* on those source
roads. The original donor graph has a separate alternative forward edge:
full branch-aware checkpoint, recovery and AI behavior must use its
original neighbors/progress IDs, not infer lanes from the preview.

## 32X private surfaces — avoid invented filled-in roads

The experimental 32X image currently embeds real decoded 68000 road-mask
and height samples, but the previous map preview painted a rectangle for
every nonempty 64×64 source lookup tile. Many tiles contain mostly
unsupported/offroad samples: painting the entire tile hides real gaps and
falsely thickens roads.

The preview now examines the 8×8 source sample tile in four 4×4 quarters
and plots a quarter **only if the verified mask contains road there**.
This preserves more source branches/gaps at the same nominal thumbnail
resolution, with no extra ROM tables, framebuffer or RAM allocation.
An independent synthetic C regression asserts that an empty upper-left
quarter stays blank while the other three render. No study route is used
to paint 32X map coverage.

**This is not an official Deluxe five-course recreation yet.** Existing
32X slots remain source-backed exploratory free-drives, not proven named
Big Forest / Bay Bridge / Acropolis / Highland / Sand Park layouts. The
five bank/table slots must *not* be assumed to equal the five named
tracks. The 68000 disassembly's `track_data_index_calc_table_lookup`
indexes tables via `$C8A0` (mode ×4), while
`game_mode_track_config` stores track number separately in
`$C8CC` (track ×4). Their exact runtime relationship needs cartridge
tracing before naming course slots.

## Requirements for actual 32X original course copies

1. Trace which normal/alternate 68000 lookup grid, 32X scene mesh,
   and vehicle/track configurations load for each **named** course;
   record mode, track and alternate flags at runtime, not just pointer
   guesses or bitmap resemblance.
2. Decode each original source course's full **render geometry, scenery,
   start/finish, pit junctions and drivable route graph**. Validate
   crossing/intersection topology, elevation and source start orientation.
3. Produce ROM-private generated assets with exact checksum gates, a
   source-to-GBA coordinate transform and acceptance images against
   original emulator captures; keep staging paths clearly distinct.
4. Restore competitive laps only after verified start/checkpoints, pit
   branches, collision and recovery work in whole-course controller tests.
5. Measure course-select redraw cost, full-lap GBA FPS and RAM budgets on
   private donor builds before replacing the experimental version.
   Preserve the standard custom-only and Genesis-original build paths.

## Reproducing this checkpoint

Source-only tests and standard build (no donor data):
```sh
make test
make -j2
```

Optional originals, using only an owner's locally supplied verified source:
```sh
VR_ROM="/path/to/Virtua Racing (USA).md" make vr-original
VR_ROM="/path/to/Virtua Racing (USA).md" VR32X_ROM="/path/to/Virtua Racing Deluxe (USA).32x" make vr-deluxe-study
```

The new tests use hand-made synthetic masks. **Source-only CI cannot
verify original cartridge silhouette parity or 32X authenticity** without
the user's legally held donor files. Performance and private-ROM smoke
results are intentionally not claimed at this checkpoint.
