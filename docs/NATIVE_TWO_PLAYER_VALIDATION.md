# Native two-player response validation

## Outcome

The four existing drive/steering profiles now have bounded validation in the
original game's **2Players VS.** mode on **Beginner, Medium, and Expert**.
All 12 observed sessions passed, and each has an unobserved twin with
byte-identical per-frame telemetry and SVP command telemetry.

Each session ran for **5,501 video frames**, with **3,363–3,552 active-driving
frames**. The observed runs cover **66,012 video frames**, **41,636 active
frames**, **17,324 steering calls**, and **17,324 longitudinal-response calls**.
The additional 12 twins bring the executed total to **132,024 video frames**.
These counts describe this two-player pass, separately from the earlier
single-player and isolated-native-call evidence.

No No Tracin' source or existing native-profile binary was changed. The
original cartridge SHA256 was checked before and after execution. Private
test fixtures reproduce the four already-supported hashes exactly and are
excluded from the evidence archive.

### Unchanged original and profile hashes

| Drive divisor | Steering step | SHA256 |
|---:|---:|---|
| 225 | 32 | `291e74f9b4d5a2c0f2b7cbf6f2e0fbf72c16cb6d83b1f58dda78209f9a64c486` |
| 180 | 32 | `c12cb153b7e82854c2e6d23abbe24d981d34a69c5f07a8370fc228bc17738cc3` |
| 225 | 48 | `5c70dafd5dadc84f9a0bcacfd52e75605b67c09c92953cc56e63150be123c4ca` |
| 180 | 48 | `6a2a7b06eef9850a803cb2240a5c28b5ddb188612a16203905fef2609b96d367` |

The first row is the supplied original. The three changed profiles are the
existing supported outputs, reproduced only as private test fixtures. The
archive's `MANIFEST.json` records each distributed evidence file's SHA256 and
byte length; the archive's own SHA256 is supplied separately.

## Native execution and the two controllers

The test links the standard Genesis Plus GX 68000/SVP objects from
VirtuaRacingMDEnhanced commit
`8004693ed487c7ad74b3f3e560780a5f3180287d`. It runs the cartridge's own CPU,
physics, and SVP rendering. It does not link the host GPU substitute or change
CPU/SVP clocks.

The separate frontend `core_two.c` differs from the checkout's frontend in
exactly two additions: a `core_pad2` input variable and assignment of that
variable to `input.pad[4]` in `sdl_input_update`. Upstream Genesis Plus GX maps
the second ordinary gamepad to index 4; both controller ports are already
configured as gamepads by its defaults. Player 1 remains at `input.pad[0]`.

All gameplay uses controller input. The probe reads native loop, countdown,
and menu flags to decide when to press buttons. It never writes guest RAM,
forces a PC/register value, skips the native countdown, or injects a routine
call. The instruction observer only reads state. The upstream checkout was
unchanged after the run.

The actual split-screen race loop is **`024D2E`**. Both human-controlled
records call the shared steering and drive paths:

| Player | Observed A0 | Steering entry | Steering return address | Drive DIVS | Drive return address |
|---|---|---|---|---|---|
| 1 | `FF9000` | `029622` | `027126` | `0295CE` | `02712A` |
| 2 | `FF9F00` | `029622` | `027126` | `0295CE` | `02712A` |

The second player is therefore covered by the selected coefficients through
the real shared routines. This was established through actual two-player
execution, rather than assuming a player-record address from the one-player
opponent layout.

## Exact input protocol

Every boot starts from the cartridge's default menu selections. The probe
presses Start in the attract loop after frame 900, waits for the mode menu
(`030416`), presses Right twice to highlight **2Players VS.**, and presses
Start through its native selection animation. In the two-player settings
loop (`0312F6`), it presses Right zero, one, or two times for Beginner, Medium,
or Expert, then presses Start on both controllers.

Menu presses last six video frames and are spaced by at least 90 frames.
The probe waits at least 80 frames after a loop transition and observes the
native menu-ready flag. Because the response patches also affect attract
gameplay, absolute transition frames differ by profile; each output CSV
records the exact input state on every video frame. `commands.json` records
the exact invocations. The menus retain **five laps**, **handicap 1 for both
players**, and **automatic transmission for both players**.

`run_frame = 0` is the first probe frame after the original countdown reaches
its active state. During the countdown Player 1 holds B, while Player 2 is
released. The following ranges are inclusive; B accelerates and A brakes.

| Player 1 run frames | Buttons |
|---|---|
| 0–199 | B |
| 200–279 | A |
| 280–359 | Released |
| 360–399 | B + Left |
| 400–419 | B |
| 420–459 | B + Right |
| 460–479 | B |
| 480–539 | B + Left |
| 540–599 | B + Right |
| 600–619 | B + Left + Right |
| 620–679 | B |
| 680 onward | Released |

| Player 2 run frames | Buttons |
|---|---|
| 0–99 | Released |
| 100–229 | B |
| 230–299 | A |
| 300–379 | B |
| 380–439 | B + Right |
| 440–459 | B |
| 460–519 | B + Left |
| 520–579 | B + Right |
| 580–639 | B + Left |
| 640–659 | B + Left + Right |
| 660–719 | B |
| 720–779 | A |
| 780 onward | Released |

The two controllers differ for **510 active video frames in each session**.
Their native throttle, brake, steering, and speed fields are observed
separately. All player/profile/course combinations cover acceleration,
full braking, release, both steering directions, direct reversal, and both
directions together.

## Response results

Both players reproduce the expected first six steering outputs. Player 1's
first pulse is positive; Player 2's first pulse is negative. These are native
internal units, not degrees.

| Setting | Absolute values of first six steering outputs |
|---|---|
| Step 32 | 8192, 12288, 18432, 25472, 28992, 30752 |
| Step 48 | 12288, 18432, 25472, 28992, 30752, 31632 |

The target reaches magnitude 127 on the fourth executed update for step 32
and the third for step 48. Across the 12 player/course/divisor comparisons,
the quicker step reaches the target cap **four or five observed video frames
earlier**. The measured video interval is not a promise of a fixed game-update
rate.

Every observed steering result retains target bounds **−127 through +127**
and stays inside the original steering-state bounds **−32512 through +32511**.
The first executed update after a direction is released returns both target
and steering output to zero. Both-directions input also returns zero. A
direct left-to-right reversal seeds **−8192** at step 32 or **−12288** at
step 48 for both players. Later released intervals remain neutral. The
finite holds in this pass do not independently remeasure the infinite-hold
converged limit from the earlier native-call matrix.

Native throttle stays within **−51 through 255**, and brake stays within
**0 through 255**, for both records. Every player in every run reaches full
braking. No observed player speed is negative. The different histories
produce distinct player state, including Player 2 remaining released while
Player 1 initially accelerates.

The observer samples `D1` immediately before the actual `DIVS` at `0295CE`
and immediately afterward at `0295D2`. All **17,324** active drive calls
produce the signed truncating quotient required by the selected **225 or
180** divisor, for both players. No host drive equation is substituted into
the game; the calculation in the validator only checks the native result.

The complete per-frame telemetry before the first steering input is also
identical between steps 32 and 48 at a fixed drive divisor, on every course.
After steering starts, trajectory and handling can diverge through the
original game's downstream code; this validation makes no lap-time or
balance claim.

## Continuity, rendering, and observer equivalence

All active frames remain in the expected two-player loop and selected course.
There are **zero stopped-CPU observations**. Actual split-screen rendering
uses **SVP command 4**: **8,672 active command-4 jobs** were observed. The
legacy command-2 counter used for one-player rendering is consequently zero
in these races; that counter does not describe the two-player renderer.

Each observed run has an unobserved build using the same inputs and normal
core. All **12 per-frame CSV pairs** and all **12 SVP-command CSV pairs** are
byte-identical. This checks that the instruction observer did not change the
measured simulation or command stream.

The screenshots are actual framebuffer captures at **active run frame 500**.
They show both cars, split-screen HUDs, course scenery, and the selected
course map for all 12 cases. `screenshots/contact-sheet.png` places the
original, drive-only, steering-only, and combined profiles in rows and the
three courses in columns. Individual original PPM captures and lossless PNG
conversions are included.

Detailed run counts, scope addresses, response samples, ranges, and telemetry
hashes are in `validation-summary.json`. Per-frame inputs and raw observations
are in `evidence/`; matching plain-build records are in
`observer-equivalence/`.

## Limits of this result

This extends the existing supported-profile scope to bounded, ordinary
two-player sessions on all three courses. It does not establish completed-race
balance, competitive driving quality, physical Mega Drive behavior, recorded
playback mode or replay compatibility, other regional/revision images, or every handicap and
transmission configuration. The native profiles and their published hashes
remain unchanged.
