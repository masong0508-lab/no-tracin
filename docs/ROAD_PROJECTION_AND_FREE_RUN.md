# Road-projection recovery and Free Run work reduction

Checkpoint: 2026-11-22. This follows the
[driving-consistency report](DRIVING_CONSISTENCY_VALIDATION.md). The original
Mega Drive ROM remains unchanged. Original courses remain an optional second
set, with the original selection and save formats preserved.

## Confirmed regression

Previously, `race_step` only updated `wrong_way` when `track_find` returned
a road. Losing the road could therefore leave a WRONG WAY warning visible
indefinitely, or preserve an unfinished 45-step streak. The regression was
added before the fix: [GitHub Actions 45032689751](https://github.com/masong0508-lab/no-tracin/actions/runs/45032689751)
fails on `wrong-way-projection-loss` in both configurations. The strengthened
exact overload-remainder assertion passed that pre-fix run.

The corrected code clears both warning and counter when road projection
fails, without touching earned checkpoint/lap credit or last good road
distance. On reacquisition, the player again needs 46 consecutive backward
grounded movement updates to show WRONG WAY. New production-code tests
cover both course sets, Arcade and Free Run, active and pending warnings,
road loss during countdown, and reacquisition.

## Work removed from Free Run

Free Run does not render or simulate CPU opponents. Initialization now skips
the rival-width scan, racing-line speed table and seven rival poses. Per-step
rival simulation immediately clears any leftover drafting flag and returns,
without the previously redundant `car_speed` integer square root.
Arcade still performs all of its normal rival calculations. A production
C host fixture verifies untouched racing-only state, zero Free Run speed
queries and cleared slipstream.

The scheduler tests now assert the *exact* elapsed-cycle remainder modulo
the selected physics interval under a four-step overload, not merely that
the remainder is positive. All three game speeds and airborne slow motion
are covered.

No new heap, framebuffer, imported commercial data or persistent memory
allocation is introduced. CPU work is reduced by construction; a GBA
performance percentage is not claimed without comparable emulator traces.

## Reproducing

Run `make test`; to include tests that require the verified locally owned
original ROM, set `VR_ROM`. CI builds the source-only standard GBA game
and uploads `no-tracin-standard-gba` for 30 days. This artifact does not
include the original commercial courses. To build their optional local
pack from the verified ROM:

```sh
make vr-original VR_ROM="/path/to/Virtua Racing (USA).md"
```

The performance numbers from the earlier driving report are historical;
they have not been replaced by a new mGBA or physical-hardware benchmark.
