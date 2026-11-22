# Fast road projection and signed fixed-point audit

Checkpoint: November 22, 2026. This continuation is based on the verified
original US ROM and the prior No Tracin' branch. The imported three original
courses remain optional. The donor ROM, imported commercial data and derived
ROMs are not included in the public source.

## Performance-oriented change

The old nearest-road query always calculated road elevation, including one
original-ROM plane lookup and signed division on the imported courses, even
when a caller needed only road segment, progress, lateral distance and width.

The new track_find_road() shares the original projection and sets h=0. The
full track_find() retains its previous elevation contract. Race progress,
soft-body segment selection, surface, road gradient and barrier collision
call the height-free variant. True height/ground-plane queries retain the
full height computation.

A production C test compares both query variants on sampled road nodes and
positive/negative lateral offsets on all three custom courses and, when the
verified donor image is supplied, all three imported courses. Route fields
are identical at each sample. In the original-course build a link-time probe
counts zero donor height queries for a successful height-free call, versus
one for the full call. Synthetic negative-elevation geometry also verifies
the full signed height result.

This eliminates redundant height calculations by construction, but **does
not establish a percentage improvement in rendered GBA FPS**. A matched
mGBA run is still required for a defensible measured frame-rate change.

## Undefined signed-shift correction

GCC's shift sanitizer reproduced actual undefined negative shifts in Arcade
grid setup and soft-body frame setup. We changed the affected signed Q8/Q14
shifts (car steering, rotations, frame rest coordinates, course normals,
race-grid progress and collision normals) to equivalent multiplication.
The multiplication stays within the intended fixed-point ranges for the
tested states. Negative road heights use a wide intermediate for their
interpolation. Existing static data, track topology, physics state structs
and save format are unchanged.

With shift sanitization enabled, thirteen production race-flow scenarios
passed, including wrong-way/reverse, close finishes and respawns. Existing
impact arithmetic tests now include shift sanitization, with both normal
and optional configurations passing.

## Reproduction and scope

Use make test for source-only checks; VR_ROM pointing at your verified US
original image enables private donor-backed tests. The new projection test
uses generated original-course headers in a temporary directory and does
not commit them. The source-only pipeline skips donor-required tests.

Local alternate-toolchain builds (Clang 17 ARM mode) completed for standard
and optional configurations. The optional binary holds 1,670,720 bytes and
has static IWRAM 28,556 / 32,512 bytes and EWRAM 229,524 / 262,144 bytes.
These are linked regions, not peak stack measurements. The imported build
was **not validated in a running GBA emulator in this pass**. It must be
treated as experimental until normal game/menu playthroughs and FPS traces
are collected. The normal GNU ARM GCC CI output is the authoritative
standard source-only build.
