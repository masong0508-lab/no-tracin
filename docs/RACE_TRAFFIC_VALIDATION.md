# Race traffic and faster original-course validation

Checkpoint: November 21, 2026. This continues the
[original-course performance milestone](ORIGINAL_COURSES_CONTINUATION.md).
It fixes physical traffic across laps, road-position changes after resets,
and soft-body slipstream persistence. The
[native Mega Drive response options](VR_CONTROLS_VALIDATION.md) are a separate
output running the original engine.

## Final build and source checks

| Property | Value |
|---|---|
| Output | `build/vr-original/notracin-vr-original.gba` |
| Size | 1,595,352 bytes |
| SHA256 | `cdb7fe90f45a201421e7ea08b9e43e7af750bf63e3f6b77b65a4a86b997115f5` |
| Compiler | GNU Arm Embedded GCC 12.2.1 |
| Emulator | mGBA 0.10.5, native GBA timing |
| Linked IWRAM | 27,340 bytes of 32,512 |
| Linked EWRAM | 229,544 bytes of 262,144 |

The ROM is 424 bytes larger than the previous `ebe40450...` milestone.
Linked RAM use is unchanged. There is no additional framebuffer or heap;
the IWRAM figure does not measure maximum runtime stack use.

All **45 repository tests passed with the supported source image**, including
the final post-review reset/rejoin fix and fortified host libc headers. The
source-only suite has 32 passing tests and 13 donor-dependent skips. Both
standard and optional ARM configurations compile. No source ROM, generated
commercial course tables or private imported binary is committed.

## Physical traffic across laps

Previously the opponent loop used total race progress to decide whether two
cars were physically near. Two cars at the same road position but separated
by one completed lap could therefore skip following, drafting and collision.

The fix wraps physical gaps by the course's lap length. The usual nearby case
avoids division. Race order and rubber banding still use total progress, so
a lapped car remains correctly separated in the standings. Collision speed
response also uses physical ahead/behind order.

### Actual controller reproduction

On the previous build, a controller drove the first Big Forest Arcade lap,
then stopped on the following straight using the brakes. At native frame
7996, rival 1 passed within **7.05 world units** of the player's centre,
with a vertical gap of only 0.109 units. Its total progress was one lap ahead,
but its physical gap was about 20 road units. There was no hit or damage.
Another rival passed within 13.09 world units with the same missing response.
No game state or timer was written during the scenario.

The final build completed the same adaptive drive-and-stop scenario. The
first three lapping opponents reached minimum centre distances of **62.41,
55.18 and 63.79 units**. None sampled below the fixture's 54-unit centre
overlap threshold. Following and real contact now affect the encounter:
the player is pushed/spun and ends at damage indicator **10/100**, with no
detached wheels or emulator errors. The ordinary clock eventually expires
because the test deliberately stops driving.

The centre threshold is a diagnostic, not a replacement for the actual
multi-part collision geometry. Nose/tail contacts can occur at larger centre
distances. This check establishes physical response, not collision immunity.

The controller is identical across builds, but feedback chooses inputs from
each build's actual state. The final encounter's own recorded inputs replay
exactly. An old build's fixed input stream is not a valid matched encounter
after timing/physics changes; its divergence is retained separately in the
evidence rather than counted as a passing comparison.

## Physical position after reset/rejoin

Review found a related preexisting issue: the race deliberately ignores large
projected jumps for race credit. After a reset, total progress can differ from
the car's current road coordinate. Wrapping that biased total still gives the
wrong physical traffic gap.

`race_step` now passes the road distance from its existing projection to
the opponent loop. If projection fails, traffic uses the last successful
road distance. No extra track lookup or persistent state is added.
Standings, rubber banding, checkpoint credit and finish rules retain their
existing total-progress semantics.

The regression places the player at road distance 1500 with retained race
progress 2000 and a rival at 1600. Drafting correctly sees the rival 100 units
ahead, while standings still place the player ahead. Movement to 1510 then
advances retained progress to 2010. Other cases cover following a stopped
relocated player, nearby pose updates, lookup failure and unchanged race-gap
rubber banding. Four cases failed in each build before this correction;
all passed afterward.

## Slipstream reaches persistent motion

The previous drafting code increased only `Car.vx/vz`. The default soft-body
simulation stores velocity in node position differences and overwrote those
exported fields on the next step. A production-code probe showed exported
speed 1997 becoming 1998, followed by byte-identical car and node states in
boosted/unboosted runs at the next step: the effect was discarded.

`sb_add_velocity` now applies the same horizontal addition to the owning car
and to the previous positions of all attached nodes. It preserves current
shape, deformation and relative/angular motion. Detached wings/hubs, another
car and invalid/null owners are unaffected. The conventional car model keeps
its existing velocity addition.

Tests run real soft-body stepping and collision code, not a replacement
physics formula. They prove additional forward displacement and velocity
persist after drafting. Stock and optional fixtures cover lap offsets through
two laps, start-line crossings, same/different lanes, front/rear contacts,
both collision models, distant pose throttling, total standings/rubber band,
deformed nodes and detached parts. The new fixtures failed before the fixes.

## Faster laps and complete Arcade races

All six final runs start through ordinary menus. A read-only feedback
controller sends acceleration, braking and steering inputs. It never writes
car state, route progress, race state or the clock; it does not teleport,
inject functions or request recovery during driving.

The controller caps straights at approximately 67 MPH and retains a
conservative curve/braking envelope. Observed maximum speed before HUD
rounding was 66.41–66.71 MPH. These are driving-continuity checks, not
maximum-speed, nitro or winning-pace tests.

| Course | Faster Free Run native frames | Free Run fps | Full Arcade native frames | Arcade fps | Clock remaining at goal |
|---|---:|---:|---:|---:|---:|
| Big Forest | 3,722 | 19.91452 | 11,183 | 18.76265 | 39.52 s |
| Bay Bridge | 3,863 | 19.26494 | 11,717 | 17.57621 | 34.78 s |
| Acropolis | 4,856 | 19.67957 | 14,618 | 18.61530 | 10.73 s |

Each Free Run completes one lap. Each Arcade run completes **all three laps,
three half-lap checkpoints, and the native GOAL state**, with seven observed
opponents. Every run finishes with zero accumulated damage, detached wheels,
controller recoveries and emulator errors. Some minor contact impulses can
occur without accumulated damage.

The controller finishes **eighth in all three Arcade races**. This validates
the finish/clock/progression path and ordinary driving continuity. It does not
establish competitive opponent balance or passing skill. Rendering FPS is per
emulated GBA second, not the host machine's execution rate.

All six driving/race input streams and the stopped-car encounter were replayed
from power-on without the feedback controller. All **seven exact replays**
match ending frame/PC, draw count, car state, race state, opponent state,
damage/wheel state and framebuffer bytes.

## Evidence and remaining scope

The validation download preserves baseline, interim and final measurements.
`gba-validation/controls-pass/candidate-final/summary.json` consolidates the
shipped build's results, including binary/ELF and harness identities. The
root controls-pass directory is the prior `ebe40450...` baseline;
`candidate-traffic/` is the interim `0d8dd084...` build before the physical
reset-position correction. Their reports should not be relabeled as final.

Research harnesses depend on mGBA development headers/library and the matching
ELF. Session scripts contain local paths and resolved addresses; use the
included candidate preparation script when adapting another compiled build.
Portable source checks remain available through `make test`; donor-dependent
checks are enabled with `VR_ROM` pointing to the supported original image.

Physical GBA hardware, competitive race balance, every physics option,
maximum-speed/nitro driving and every collision case remain untested.
Pit-service and alternate-route race logic, larger custom maps and layered
road formats remain future engine work. The original courses remain optional
under **VR ORIGINALS**, selected with **L/R** on the Arcade/Free Run course
screen; custom courses remain the initial selection.
