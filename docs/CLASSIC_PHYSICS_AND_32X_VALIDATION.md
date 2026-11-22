# Classic handling profiles and 32X import-readiness validation

Checkpoint: 2026-11-22. No Tracin' branch `improve/original-virtua-courses-20261121`.

## Scope and provenance

The US Sega Genesis Race Drivin' cartridge has since been supplied and
verified, but the Atari arcade TMS34010 physics ROM/source and the
Genesis cartridge's specific dynamics routines have not been recovered. **HARD STYLE** and **RACE STYLE** are new original GBA C
approximations, **not Atari physics code**. The existing SOFT-BODY and
ARCADE modes are preserved. The four choices occupy the existing option
byte 14 within the original 16-byte SRAM options record: no Car, Race,
course-record, or save-layout changes.

HARD STYLE uses less transient grip, reduced high-speed steering lock and a
slower inertial turn-in. RACE STYLE has greater grip, faster steering and a
wider cornering envelope. Both reuse ramps, collisions, crash recovery,
stunts, tuned engine/brakes, tires, handbrake, gravity and adjustable
downforce. The bounded fixed-point yaw calculation avoids wide division
in the GBA physics path.

## Actual validation

- New production host-C driving fixture passes in **both standard and
  optional configurations** with shift, signed-overflow and divide-by-zero
  sanitizers. It compares the original ARCADE state exactly across runs
  and checks different HARD/RACE turning, braking, reverse and steering
  release.
- Four 32X metadata probe tests pass: valid synthetic header and hash,
  rejection of the Mega Drive cart, short or hash-mismatched input
  rejection, and read-only CLI execution.
- GitHub Actions discovers **68 tests**, with **54 source-only passed**
  and **14 private-donor-dependent skipped**, and builds the standard
  GBA ROM successfully.
- Controller-only mGBA test on the standard game navigates through
  Options, selects values 2 and 3, checks live and saved-option state,
  enters Arcade, drives, renders and reports **zero emulator errors**.
- Controller-only mGBA tests on the private original-course GCC build
  select both new modes and **all three Genesis original courses**:
  Big Forest (course 3), Bay Bridge (4) and Acropolis (5). All **six**
  smoke runs select correctly, move forward relative to initial progress,
  render actual framebuffers and finish the test with zero errors.
- The source Genesis/SVP US ROM remains unmodified; its verified SHA-256 is
  `291e74f9b4d5a2c0f2b7cbf6f2e0fbf72c16cb6d83b1f58dda78209f9a64c486`.
- Private optional GBA: **1,598,456 bytes**;
  SHA-256 `dea57e9a4d3b6ce4a07a32a57ac5fb405c01f9d8bca450e10f49b98b08c6b121`.
  GNU ARM EABI GCC 14.2.1; linked IWRAM **27,536 / 32,512** bytes,
  EWRAM **229,544 / 262,144**. No heap or framebuffer added.

These are *smoke tests*, not three-lap Arcade wins or parity with Atari's
original game. Previous FPS measurements are historical. This checkpoint
makes no new long-run FPS or physical-hardware claim.

## Upcoming 32X Deluxe work

The 32X US cartridge is now supplied and verified by whole-file SHA256 and
Sega checksum (see `NEWTON_MOTION_AND_ROM_INPUTS.md`).
`tools/probe_vr_32x.py` reports metadata only; it still cannot decode or
import Deluxe course data. The next authorized-ROM milestone must independently
reverse-engineer five original circuits (including Highland and Sand Park),
three vehicle classes, tunnel/pit paths, multiple branches and larger scenery
tables. The fixed three-courses-per-set UI and records must be generalized
without altering legacy save identities. The existing Mega Drive importer
must not be used with a 32X file just because both say Virtua Racing.

## Authentic Atari code is a separate research milestone

Atari hardware engineer Jed Margolin describes original Hard Drivin' and
Race Drivin' physics running on a dedicated TMS34010, with a vehicle model
developed with Doug Milliken. See
https://www.jmargolin.com/schem/schems.htm .
Authentic source-level parity would need an authorized original ROM/source,
correct CPU mapping, car-state traces, transmission/force curves, tire slip
and suspension behavior, and regression tests across multiple scenes.
The playable GBA modes remain accurately labeled approximations until then.
