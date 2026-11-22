# 32X Virtua Racing Deluxe and Hard/Race Drivin' physics groundwork

Checkpoint: November 22, 2026. This document distinguishes features we can
implement and test today from features that require the user's forthcoming
32X ROM or inspectable Hard Drivin'/Race Drivin' physics code.

## What the additional 32X cart offers

Public documentation of **Virtua Racing Deluxe** for Sega 32X describes **five
tracks**: Big Forest, Bay Bridge, Acropolis, **Highland** (tight city and
highway bends) and **Sand Park** (desert, tunnel and multiple routes). The
32X game also provides Formula, Stock and Prototype vehicle classes with
meaningfully different handling, better detailed scenery, time trial and
split-screen two-player. The original game's extra cars and maps must not
be confused with content in our current three-track US Genesis/SVP importer.

References: Sega's 32X Deluxe manual, and the game descriptions below:
- https://www.gamesdatabase.org/Media/SYSTEM/Sega_32X/Manual/formated/Virtua_Racing_Deluxe_-_1994_-_Sega.pdf
- https://rq87.flyingomelette.com/RQ/32X/vr.html
- https://www.retrogamesreview.co.uk/2015/05/virtua-racing-deluxe-32x-review.html

**The 32X source ROM has not yet been supplied.** The local `tools/probe_vr_32x.py`
uses read-only 0x100–0x200 Sega header metadata and full-image SHA-256 to
identify a file without changing it. It does not guess a course-data offset,
assemble a playable ROM, or accept the existing Genesis/SVP importer offsets.
ROMs that do not identify themselves as byte-ordered Sega 32X are rejected;
regional and format variations will require their own verification.

Once the 32X ROM arrives:
1. Record its exact file hash, byte size, region, product revision and header
   format privately; never overwrite it or commit proprietary source/data.
2. Audit course/entity tables, coordinate scale, 32X-specific transforms,
   terrain collision and route graphs **against emulator observations**.
3. Add a new importer guarded by exact full-file hash, not a fallback in
   `tools/import_vr_original.py`. Keep source hashes, pack IDs and records
   independent for each version.
4. Expand the existing fixed three-courses-per-set UI/data descriptors to
   handle a **five-course Deluxe set** without resizing legacy save structures
   or changing the user's default custom tracks.
5. Support alternate roads, pit entrances and tunnels as a graph, so the
   road projection, camera, traffic and checkpoint progression follow the
   selected branch rather than simply the closest polygon.
6. Budget meshes using the current renderer's bounded face queue, GBA RAM
   ceilings and adaptive scene distance. Improve occlusion, not simply raise
   the per-frame polygon submission cap.
7. Build/import locally and run exact-ROM tests and controller-only mGBA
   playthroughs. Release importer/metadata source, not source ROM data.

## Hard Drivin' / Race Drivin' car dynamics: code provenance

Former Atari hardware engineer Jed Margolin describes a detailed dynamic car
model developed with vehicle dynamics consultant Doug Milliken. It represents
engine, transmission, springs, dampers and tire forces in different operating
ranges, and ran on a dedicated **TMS34010** in the original Atari arcade
hardware. This is **not** the same as a Mega Drive or 32X Virtua Racing
program or as the No Tracin' existing soft-body physics.

Reference: https://www.jmargolin.com/schem/schems.htm

The original dedicated physics code has **not** been obtained or identified in
this project. MAME's hardware emulator code and board schematics provide
valuable roadmaps but do not, by themselves, supply an independently licensed
original Atari physics source-code port. We must not claim code-exact physics
until we recover and validate the actual routines and tables from an authorized
source and compare real car-state traces.

## New playable handling switch (experimental, not original Atari code)

The **PHYSICS** setting now offers four options in the existing SRAM slot:
- **SOFT-BODY**: unchanged 22-node, spring-and-beam damage simulation
- **ARCADE**: unchanged No Tracin' lightweight car dynamics
- **HARD STYLE**: a bounded, deterministic, lower-grip, slower-steering,
  more-inertial bicycle-response approximation
- **RACE STYLE**: a bounded, deterministic, faster-steering,
  higher-traction bicycle-response approximation

The two new modes are original C approximations designed to create an
independent test bed for future genuine Atari car-model research. They share
existing GBA gravity, ramps, track collision, stunt and race systems. They
remain selectable on the standard custom tracks and optional Genesis-derived
courses. Choosing these modes **does not imply original Atari physics code**.

The selector uses **the existing option byte 14** (the saved system has a
16-byte options field), allowing older values 0 and 1 to load unchanged.
Neither `Car`, `Race`, option count, track records nor save schemas changed.
The physics-mode host regression executes production car/soft-body code in
both source configurations, compares the existing arcade mode exactly and
checks the two new steer/brake/reverse paths with shift/overflow sanitizers.

Before calling future code 'authentic Hard Drivin'/Race Drivin' physics', we
will need original code or ROM identity, CPU mapping, operating state/timing,
steering/gear/torque curves, tire slip and suspension response, trace
comparison on several roads, and repeatable tests that show parity. Any
unverified approximation remains clearly labeled as such.
