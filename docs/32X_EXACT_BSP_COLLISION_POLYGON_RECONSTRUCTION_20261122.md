# Virtua Racing Deluxe 32X — exact 68000 BSP collision polygons

Checkpoint: November 22, 2026 (US Central). Source work in draft PR #3.
This is a substantive step toward faithful native road geometry, NOT a
declaration that five Sega Deluxe races have been completed.

## Why the old map was insufficient

The first 32X importer sampled the authenticated source road classification
every 64 native source units, yielding an experimental 512x512 GBA road
silhouette. The sampling misses narrow branch surfaces and makes turns
look thick or blocky. Its skeleton cycle is merely a starting/preview
route and cannot establish source Sega starts, pit links, or finish lines.

Original cartridge 68000 code at file $00748C evaluates groups of four
BSP boundary comparisons in each 28-byte record, choosing the first
accepting group. The routine at $0075C8 evaluates a corresponding signed
2D height plane. These fields contain source collision boundaries,
not the original SH-2 scenery/display triangle meshes.

## New: mathematically exact source boundary inequalities

Source module: tools/vrd32x_exact_bsp.py

Each group is expressed as four exact integer half-plane inequalities
in native 0..511 tile coordinates. The four branch families preserve
signed comparisons, strict/inclusive inequalities, negative slopes,
and the arithmetic right shift of the original 68000.

For example, the source comparison

    2*x < ((2*z*k + 512*c) >> 9)

is exactly equivalent on the original integer lattice to:

    1024*x - 2*k*z <= 512*c - 512

The complement is:

    -1024*x + 2*k*z <= 511 - 512*c

The source's four halfplanes are clipped to the tile using rational
intersections (fractions), not floating guessed edges. Resulting
polygons preserve original collision footprints exactly when tested
at native integer sample locations. Regions can overlap; **the
original first-matching group determines height**. The private mesh
exporter subtracts earlier accepted regions from later ones to avoid
conflicting height-plane faces. This is a first-priority partition of
the native collision plane, not an SH-2 scene mesh.

The first-match classification is compared against the original
source interpreter using synthetic groups and, when VR32X_ROM is
provided, representative real cartridge record samples.

Sources:
- https://github.com/matiaszanolli/sega-vr-disasm/blob/master/disasm/modules/68k/math/angle_normalize_bsp.asm
- https://github.com/matiaszanolli/sega-vr-disasm/blob/master/disasm/modules/68k/math/plane_eval_pair.asm

## Better source-derived 32X map previews

The private importer now verifies the original supported 32X ROM identity
once before any private output. Its exact polygon footprints determine
the entire native road-area bounding rectangle, including thin features
missed by 8x8 samples.

The GBA course selection thumbnail now queries the ORIGINAL 68000 BSP
evaluator at the projected 102x68 pixel locations and four bounded
subpixel probes, rather than rebuilding the silhouette from coarse
height-grid centres or an invented staging loop. It still saves the
finished map as an 867-byte ROM-only bitmap per mode. There is no
per-frame 68000 parser, extra framebuffer, heap or EWRAM allocation.

The GBA driving surface and 3D graphics remain experimental; a more
accurate 2D preview is not itself a more accurate gameplay collision mesh.

## Native vector-map evidence, private to the owner

Run the read-only, supported original-ROM exporter:

    make vr32x-original-bsp-evidence VR32X_ROM="/private/original.32x"

It writes five 2D vector SVG files, five priority-resolved 3D
collision-surface OBJ files, and a mode audit manifest under the
ignored build/vrd32x-bsp directory. The exact source-hash gate is
mandatory. OBJ X/Z coordinates retain original source collision
boundaries. The native signed height plane is extended continuously
over its polygon faces, then divided by an explicitly **unverified**
view-only height scale. No original 32X SH-2 display mesh, source
material, pit/AI route, named course or calibrated height scale is
claimed. All SVG/OBJ files are private derived materials and excluded
from the public repository.

The source-only tests use invented 28-byte boundary records; no
commercial ROM bytes or derived authentic course files are in GitHub.

## Remaining acceptance gates for five finished 32X Deluxe courses

1. Verify the actual mode-to-named-course mapping using original
   68000/SH-2 gameplay traces, especially the separate original indices
   at $C8A0 (lookup mode) and $C8CC (track/scene). Five readable mode
   tables are not proof of five named tracks.
2. Decode native SH-2 track display geometry/scenery, start grids,
   pit entrances, branch junctions, course progress and finish points.
3. Validate source unit conversion, overlap/height priority and roads
   at multi-level structures against original 32X emulator captures.
4. Build the owner-private combined GBA ROM and test entire races, FPS
   and RAM budgets; physical GBA tests remain a separate gate.

Source-based road boundaries and documentation can be finished without
a private ROM in this environment, but original game assets, exact race
identities and 3D race completion cannot honestly be certified absent
the original donor images and emulator evidence.
