# Driving Consistency Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans
> to implement this plan task by task. Independent audit and emulator tasks
> provide evidence; the root integrates production changes and owns review.

**Goal:** Correct demonstrated timing, impact, race direction, finishing and
respawn inconsistencies, then deliver a tested playable update.

**Architecture:** Preserve the current C engine and data layouts. Move mutable
input policy to physics-step boundaries, widen unsafe intermediates, and
keep race progress and finish ordering tied to their actual events.

**Tech Stack:** C99, ARM7TDMI/GCC 12.2.1, Python unittest, mGBA 0.10.5,
Genesis Plus GX for optional native-controller coverage.

**Spec:** `docs/superpowers/specs/2026-11-22-driving-consistency-design.md`

## Global Constraints

- Original ROM unchanged; imported assets stay outside tracked source.
- Optional original maps and separate record-bank identities remain.
- Preserve `STEP_CYCLES=280896`, four-step cap, native patch profile hashes.
- No framebuffer/heap addition; two 32-bit respawn snapshots and two previous
  Q8-position values for the reviewed physical finish-plane correction.
- Existing draft PR remains unmerged.

## Review Focus

- A race ending midway through a draw immediately stops held steering/nitro.
- A genuine overload is bounded while its fractional time is retained.
- Zero-crumple and low-gravity/high-grip impacts retain finite physical state.
- Same-step finish fractions and exact ties preserve chronological results.
- Respawn across an already earned checkpoint/lap never duplicates an award.

### Task 1: Make executed physics steps independent of draw grouping

**Files:** `src/main.c`; new `tests/game_loop_host.c`,
`tests/test_game_loop.py`.

**Interfaces:** Keep `car_step`, `game_step`, `race_step` and main-loop locals
unchanged. The test extracts the production scheduling block and constants;
real stepping runs beneath it, with hardware/world observation stubs only.

- [x] Add six boundary cases (fuel, countdown, crash, recovery, goal, timeout)
  comparing four one-step frames with one four-step frame; assert matching
  executed inputs, boost, fuel and final car/race state.
- [x] Add cadence cases through four steps per draw and an overload case;
  assert expected elapsed-step count below the cap and bounded backlog above.
- [x] Run `python3 -m unittest discover -s tests -p test_game_loop.py -v`;
  expect the demonstrated grouping/cadence assertions to fail.
- [x] Move mutable control/boost resolution into the executed-step loop and
  retain fractional time when the four-step limit is reached.
- [x] Rerun the focused tests and complete suite; expect no failures. Commit.

### Task 2: Keep supported impact settings within arithmetic range

**Files:** `src/softbody.c`; new `tests/softbody_limits_host.c`,
`tests/test_softbody_limits.py`.

**Interfaces:** Existing soft-body APIs and node layouts remain unchanged.
Host cases call production `car_step` with real tuning and collision paths.

- [x] Add the stationary, A-only CRUMPLE=0 wall reproduction and the observed
  low-gravity/high-grip impact cases under signed-overflow/divide sanitizers.
- [x] Run the focused fixture; expect the existing multiply/square overflows.
- [x] Widen the demonstrated intermediates with ordinary-range equivalence;
  keep normal tyre processing free of new wide division.
- [x] Verify focused cases and the bounded original settings scan, then run
  the complete suite; expect no sanitizer or test failure. Commit.

### Task 3: Correct direction, finish order and earned respawn progress

**Files:** `src/race.c`, `src/race.h`, `src/car.c`; new
`tests/race_flow_host.c`, `tests/test_race_flow.py`; existing host fixtures only
where the new explicit respawn callback requires a stub.

**Interfaces:** Add `void race_on_respawn(Car *player)` to race.h, called after
`car_reset` inside `car_respawn` when a race track is active. Two private s32
values hold the safe progress/projection, initialized by `race_begin` and
updated with the existing race-safe pose. Existing structs remain unchanged.

- [x] Add reverse movement in both orientations, stationary/airborne cases,
  and warning duration/reset assertions for both physics models/course sets.
- [x] Add an earlier rival finisher, both orders within one step, exact ties,
  normal live order and frozen post-goal rank.
- [x] Add backward/forward respawn, start-line wrap, earned checkpoint/lap,
  fresh-race reset and arbitrary projection-jump cases.
- [x] Run `python3 -m unittest discover -s tests -p test_race_flow.py -v`;
  expect the demonstrated wrong-way/finish/respawn assertions to fail.
- [x] Use actual roadward velocity for wrong-way detection, snapshot rival
  progress before movement and compare crossing fractions on the goal tick,
  then connect explicit safe-progress restoration to real respawn.
- [x] Run focused and complete suites; preserve prior traffic/save checks.
  Expect all tests to pass. Commit.

### Task 4: Validate and deliver the integrated binary

**Files:** Current validation documentation, README, private deliverables and
allowlisted evidence archive. No generated course data enters source control.

**Interfaces:** Final ELF symbols configure the ordinary-input emulator probe;
the delivered GBA hash identifies every final run and replay.

- [ ] Build standard and optional GBA configurations; check linked RAM, size,
  generated custom data and source diff. Expect both builds to pass.
- [ ] Complete harder driving/nitro and full Arcade runs on all three
  original courses, plus targeted reverse/recovery checks. Observe results
  without changing gameplay RAM; record any genuine failure and fix its cause.
- [ ] Replay final input recordings against the final binary and inspect
  native-resolution captures. Report actual FPS/coverage without extrapolation.
- [ ] Obtain a fresh whole-change review, address important findings with
  failing/passing regressions, and stop optional retesting once verified.
- [ ] Publish source to the existing draft PR, verify CI, update playable
  downloads/guide/evidence, and confirm every saved artifact result.
