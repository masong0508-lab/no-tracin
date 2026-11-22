# Native Controls and Race Traffic Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Deliver independently selectable native steering/drive response and correct GBA traffic interactions across laps.

**Architecture:** Keep the existing strict native patcher and four verified profiles. Use separate original-CPU research and GBA controller runs to validate behavior. Correct GBA physical traffic distance and persistent soft-body drafting without changing race progress semantics.

**Tech Stack:** Python standard library, C/ARM7TDMI, original68000/SVP execution in Genesis Plus GX, mGBA0.10.5.

**Spec:** `docs/superpowers/specs/2026-11-21-native-controls-and-race-traffic-design.md`

## Global Constraints

- No new heap allocation or framebuffer. No new commercial asset in Git.
- Default invocation remains exactly the earlier 180/32 output.
- Original maps remain optional in No Tracin'.
- All outputs must be new and distinct from the source and each other.
- Apply accepts the exact original US image only.

## Review Focus

- Wrong/repaired-checksum source revisions must fail without writing output.
- Restore must identify the actual profile and restore every original byte.
- Nearby cars separated by a lap must collide/follow consistently across the start line.
- Race order and rubber band must still distinguish completed laps.
- Drafting must persist in attached soft-body nodes without moving detached parts or another owner.

## Task 1: Strict independent native profiles

**Files:** Modify `tools/patch_vr_response.py`, `tests/test_vr_response_patch.py`;
update `docs/VR_PHYSICS_RESEARCH.md` and README when measurements are final.

**Interfaces:** Produces `identify_profile(data) -> tuple[int,int]` and the
keyword profile options specified by the design. Existing APIs remain valid.
Research consumes the strict outputs and supplies independent execution evidence.

- [x] Add failing tests for all four profile edits/IPS/restore identities,
  rejection of step16 and wrong revisions, and conflicting restore flags.
- [x] Run `VR_ROM="..." python3 -m unittest discover -s tests -p
  'test_vr_response_patch.py' -v`; expect new-interface failures before edits.
- [x] Implement verified profiles225/32,180/32,225/48,180/48, with instruction
  guards, recomputed checksum, full output hash and automatic restore detection.
- [x] Run the same focused suite; expect all tests to pass and legacy output
  hash `c12cb153...` unchanged.
- [x] Check original CPU execution and normal input-driven runs on each course
  for all four settings; summarize exact scope and rejected step16 experiment.
- [x] Commit the tested native feature and its documentation.

## Task 2: Physical race traffic and persistent drafting

**Files:** Modify `src/race.c`, `src/softbody.c`, `src/softbody.h`; create focused
host fixtures/tests for traffic and soft-body external velocity.

**Interfaces:** Race code consumes a horizontal velocity addition provided by
softbody code when `sb_valid(player)` is true. Physical gaps wrap by course lap
length; absolute progress remains the source for race rules.

- [x] Reproduce missing lapped contacts/following/drafting in both builds and
  exported-only velocity being lost on the next real soft-body step.
- [x] Add failing tests for those cases, start-line wrap, lap-based rank,
  attached-node persistence, detached nodes, and non-owner calls.
- [x] Implement wrapped physical distance and a small ownership-checked
  soft-body velocity addition; retain the conventional car fallback.
- [x] Run focused tests; expect same physical responses across lap offsets
  and a persistent, shape-preserving boost with no detached-node change.
- [x] Build a frozen optional candidate and repeat complete Arcade/faster-lap
  checks with observed FPS, memory, race results and deterministic replay.
- [x] Commit only verified changes and their validation record.

## Task 3: Integration and delivery

**Files:** Validation docs, playing guide and generated private packages.

**Interfaces:** Consumes frozen/hash-identified outputs and evidence from both
tasks. Git contains source/tools/docs only; playable derivatives are private.

- [x] Run the complete repository suite with the supplied image and compile
  both GBA configurations; expect all tests/builds to pass.
- [x] Review the combined source diff independently and address material
  findings with focused regressions. Preserve documented limits.
- [ ] Update existing draft PR2 with a matching Git tree, verify CI success.
- [ ] Save updated GBA and native profile downloads, guide and evidence;
  verify hashes and explain the differences and remaining scope to the user.
