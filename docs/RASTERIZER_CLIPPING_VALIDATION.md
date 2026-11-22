# Rasterizer signed clipping and emulator smoke validation

November 22, 2026. This continues the No Tracin' original-course and
software-renderer optimization work. The original maps remain optional;
neither the donor ROM, course data, saves nor original native coefficients
were changed.

## Reproduced rendering error

The software polygon filler and clipper used C signed left shifts on
values which legitimately become negative:

- The initial X for a left-of-screen scanline edge
- A downward-moving edge whose horizontal slope is negative
- The numerator of an inside-to-outside near-plane intersection

C leaves those operations undefined even though common ARM toolchains
happened to produce expected two's-complement results. A direct production
renderer fixture compiled with signed-shift sanitization reproduced an
actual error on an offscreen-left polygon. The fix multiplies by the exact
positive fixed-point scale before the same rounding/shift. It retains the
same integer geometry, clipping thresholds, color and face count.

The regression exercises real fill_poly, edge_slope and project_poly using
an offscreen quad and a near-plane crossing, and passes under UBSan.
There is no added framebuffer, heap allocation or persistent state.

## Emulator test infrastructure

The new .github/workflows/emulator-smoke.yml runs an mGBA ARM CPU and Mode 4
video emulator from the standard compiled GBA ROM, with ordinary buttons
and read-only symbol-based observations. The test navigates title, mode and
course menus, starts an Arcade race, drives with the throttle and captures
the resulting native framebuffer. It asserts rendering progress, road
progress and the absence of emulator error messages.

A separate portable runtime workflow enables local, donor-backed testing
without storing the privately owned commercial ROM in GitHub. The exact
imported-course binary is not declared emulator-tested until separately
executed and verified with the portable runtime.

## Limits

Rendering is still software rasterization on GBA. These changes make
edge arithmetic defined; they do not by themselves establish higher FPS
or guarantee that every camera, game mode and physical console is correct.
A matched emulator run with the exact optional binary is needed to make
new numeric frame-rate claims.
