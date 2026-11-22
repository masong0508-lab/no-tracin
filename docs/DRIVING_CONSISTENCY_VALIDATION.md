# Driving consistency, impact limits and race recovery

November 22, 2026. This checkpoint continues the original-course and native
response work with demonstrated driving/race fixes. Original maps remain an
optional second course set. Custom circuits, Stunt City, record-bank identities
and the native Mega Drive patch profiles remain available.

## Tested build

The previous delivery at `295452d` rebuilt byte for byte with GCC 12.2.1. Its
known Big Forest drive also reproduced the same input/telemetry, complete
observed state and framebuffer in mGBA 0.10.5 before work began.

| Build | ROM bytes | Linked IWRAM | Linked EWRAM |
|---|---:|---:|---:|
| Standard custom-course build | 345,744 | 26,684 | 228,156 |
| Optional original-course build | 1,597,080 | 27,356 | 229,544 |

Optional GBA SHA256:

`361b4a7bf02a93a6507d51a41f78f9a08ac83e23111fb169b2557f68833ed4c9`

Matching ELF SHA256:

`1e0f7d5be8aef4d4604443199eaf6a273b13758418b3d4decbec09fc04f97c0e`

The optional ROM grew by 1,728 bytes from the prior delivery. Persistent
IWRAM grew by 16 bytes: two safe-progress values and two preceding-position
values. EWRAM is unchanged; no framebuffer or heap allocation was added.
These are static linked sizes, not a measured stack peak. Both configurations
compile without warnings, and regenerating the custom courses changes no
tracked data.

The supplied 2,097,152-byte original image remains unchanged:

`291e74f9b4d5a2c0f2b7cbf6f2e0fbf72c16cb6d83b1f58dda78209f9a64c486`

## Changes and observed failures

### Controls and elapsed physics time

Mutable control policy now runs before every executed physics step. An empty
nitro meter, the end of countdown, a crash/recovery, goal or timeout therefore
takes effect on the next step even when several steps share a draw. The
controller's held-button sample still belongs to that draw.

The catch-up loop retains its four-step cap. When genuine overload remains,
it discards whole excess steps but retains fractional elapsed time. Previously
it erased the fraction whenever four steps ran: a 3.75-step drawing cadence
executed 3,500 steps instead of the expected 3,750 across 1,000 draws.

The normal interval remains exactly 280,896 GBA cycles, one native video-frame
period (approximately 59.73 Hz). Turbo, Chill and airborne slow motion retain
their existing intervals. This does not promise uninterrupted real-time
physics when a scene exceeds the four-step budget.

The old GBA was also observed entering its real `car_step` with nitro zero
and boost still enabled during a variable-size batch. The final bounded
controller trace observes 102 actual entries, including 84 fuelled calls,
and zero empty-meter boost calls while the meter falls from 500 to zero.
This aggressive held-turn trace ends with HUD damage 4/100 and a brief air
state; it is a policy regression witness, separate from clean driving runs.

### Impact arithmetic at supported settings

A throttle-only drive from rest into a flat fixture wall with CRUMPLE=0%
overflowed the old angular transform at step 495 (`16380 * -147238`). Existing
low-gravity/high-grip settings could also overflow the squared tyre-force
limit, including a stationary no-slip case.

The angular products/sum are now wide before the existing rounded shift.
Tyre friction keeps the old 32-bit path when both components and the limit
are within its proven range; larger values use wide magnitude/scaling and an
integer square root. The normal tyre path adds no wide division. Sliding grip,
lab settings, deformation and wheel detachment behavior are retained.

The bounded host scan completed 612 cases without signed-overflow or
divide-by-zero reports. All 603 previously defined baseline cases kept their
final summaries. A separate 56-case comparison matched every one of 1,500
steps' car, node, beam, hub, axis and damage states, including detached-wheel
trajectories. These are production-code host fixtures, not 612 emulator laps
or an exhaustive proof for every input/setting combination.

### Direction, finish order and recovery

Wrong-way detection follows horizontal movement relative to the road tangent,
including reverse gear. It retains the speed threshold, ground requirement,
roughly 120-degree backward cone and 46-step persistence rule. Real brake-only
reverse motion is checked in both car models and both course-set compilations.

An opponent already past the goal keeps its earlier finish. On the player's
goal step, crossing fractions decide same-step order, with exact ties retaining
the player's tied place. Review found that whole-unit race credit could still
misorder close results: stock Big Forest's last segment ends at distance
32,564 while its lap length is 32,576. A player crossing at 1,408/2,057 of a
tick was incorrectly placed behind a rival crossing at 1,700/1,952.

The correction retains consecutive Q8 player positions and measures a forward
crossing of the physical finish plane. It preserves full dot-product precision
and uses cross multiplication, without division. Existing credited-progress
rules still decide lap eligibility; a credited finish without a valid physical
crossing interval retains the distance-fraction fallback. Tests cover the real
stock seam, either crossing order, subunit exact ties, earlier finishers and
the frozen result after the flag. Respawn initializes the preceding position
so a recovery jump cannot become an artificial movement interval.

`car_respawn` now restores race credit and projection paired with the saved
safe pose. Previously a 300-unit backward reset kept those 300 units as credit,
so the next checkpoint and lap could arrive 300 units before their lines.
Completed laps/checkpoints, clock, lap times and records remain earned; crossing
them again after recovery gives no duplicate award. The unrelated large
projection-jump guard remains. `Car`, `Race`, `Rival` and save layouts are
unchanged.

## Regression and review evidence

The integrated suite passes **59 tests with no skips** using the supplied
original ROM. The major fixes have recorded failing/passing regressions:

| Area | Focused evidence |
|---|---|
| Main loop | Six boundary scenarios, grouping equivalence, cadence, speed options, slow motion, timer wrap and overload |
| Impact arithmetic | Actual overflows reproduced under signed-overflow/divide sanitizers, plus exact normal/wide force vectors |
| Race flow | Reverse direction/cone/delay, finish order and ties, backward/forward recovery, earned checkpoint/lap, grid and jump guard |
| Review correction | Real Big Forest geometry with actual car stepping, subunit crossing orders and exact ties |

Most grouping cases exercise both car models; the forced crash transition uses
the simple model. The optional host race-flow compilation uses synthetic
geometry for both set indexes. Original-course terrain and end-to-end driving
are covered separately below. The prior traffic, drafting, banking, ROM import,
patch/restore and save-record regressions remain in the full suite.

A fresh independent whole-change review reproduced the close-finish problem;
its correction was verified against the pre-fix source and then the final
59-test suite. Two nonblocking follow-ups remain: a direction warning can
persist if the road projection disappears, and the scheduler fixtures could
assert more explicit outcomes and the exact overload remainder in addition
to grouping equivalence. These are retained in the evidence rather than
treated as completed work.

## Actual original-course driving

The probe boots the compiled GBA and enters modes/courses through ordinary
buttons. Its controller uses read-only route/state observations. It does not
write guest RAM, teleport, redirect the CPU, inject game calls or restore a
saved state. All nine runs use the normal soft-body model. Recorded inputs
are then replayed from boot against the same final binary.

| Scenario | Big Forest | Bay Bridge | Acropolis |
|---|---:|---:|---:|
| Nitro Free Run rendered FPS | 19.92 | 18.65 | 19.44 |
| Nitro peak MPH estimate | 105.14 | 101.49 | 95.11 |
| Full three-lap Arcade rendered FPS | 17.46 | 16.18 | 17.31 |
| Arcade reversing recoveries | 0 | 2 | 0 |

All three nitro laps and all three Arcade races completed. Every Arcade run
reached GOAL with three laps, three checkpoints and seven opponents. The
remaining runs cover brake/reverse/rejoin on Big Forest, a handbrake steering
disturbance on Bay Bridge, and the Bay Bridge drive used to establish the
variable-batch nitro trace. All nine complete without accumulated damage,
detached wheels or emulator errors. Bay Bridge's two reversing maneuvers are
reported as recoveries, not hidden as an uninterrupted clean race.

All nine own-input replays match the final native frame, CPU position, draw
count, car/race/rival state, damage/wheels and framebuffer. The reverse run
naturally activates WRONG WAY, then clears it when forward travel resumes.
Actual framebuffer captures cover the nitro peaks, reverse and handbrake
scenarios. Static captures do not rule out every transient rendering artifact.

Rendering FPS is per emulated GBA second, not host throughput. The previous
checkpoint's same-controller Arcade figures were 18.76, 17.58 and 18.62 FPS.
This update improves correctness with a measured rendering cost; it does not
claim a speed increase. Corrected cadence and wider impact arithmetic both
change CPU demand, and these comparisons do not isolate their individual cost.
Bay Bridge's heavier Arcade run still drops whole physics backlog under the
retained cap; the final report records the tick deficit with a conservative
four-tick endpoint uncertainty. The other eight endpoints fall within that
uncertainty. No universal exact-cadence claim is made under overload.

## Native Mega Drive continuation and limits

The existing native profiles also pass **12 two-player sessions plus 12
unobserved twins**, covering all four drive/steering combinations on all three
courses. Both players execute the shared native routines. The pass verifies
17,324 steering and 17,324 drive calls, 8,672 active split-screen SVP jobs,
and identical observed/plain telemetry. See
[the native two-player report](NATIVE_TWO_PLAYER_VALIDATION.md).

Physical GBA/Mega Drive hardware, exhaustive lab/input combinations, competitive
race balance, native completed two-player races/recorded playback, and pit
service remain outside this checkpoint. Larger maps and replacement physics
are still future engine work. The updated private guide and evidence archive
include the exact binary identities, controller recordings, source fixtures,
native checkpoint and earlier milestone. Commercial ROMs, generated imported
course headers, guest RAM dumps and compiled emulator cores are excluded.
