# Driving consistency and race recovery

## Intent

Continue the user's authorized testing and improvement of No Tracin'. Make
existing driving and race rules behave consistently during hard driving,
recovery and slower drawing. Original Virtua Racing courses stay optional;
custom courses, Stunt City, native response profiles and saves remain usable.

The baseline is commit `295452dcfa2edcfcb90ac124bba846287b7a250e`. The supplied
ROM and delivered GBA were recovered by their verified hashes, and GCC 12.2.1
rebuilds the previous GBA byte for byte. All 45 baseline host tests pass.

## Observed problems

- The four-step drawing catch-up limit discards fractional time even when the
  workload is within its budget. A 3.75-step draw cadence executes 3,500 steps
  instead of 3,750 over 1,000 draws.
- Nitro and end-of-race controls are sampled before the physics batch. Fuel
  depletion, countdown, crash/recovery, goal and timeout therefore depend on
  how many physics steps share a draw.
- Disabling crumpling can overflow the angular impulse transform in an
  ordinary throttle-only wall impact. A supported low-gravity/high-grip setup
  can also overflow the tyre friction-circle calculation after an impact.
- Wrong-way warnings use body heading, producing the opposite result during
  reversing. A rival that finished earlier can lose its earned place while
  the player crosses the line on a later update.
- Respawning behind the car's last road projection retains that lost distance
  as race credit, moving future checkpoint/lap awards away from their lines.

## Approach and engineering choices

Keep the existing engine and repair the demonstrated state boundaries. A
whole physics replacement would combine unvalidated handling changes with
correctness fixes; limiting controls or lab settings would hide supported
behavior. The bounded repair approach is the implementation choice for this
continuation, under the user's existing authorization to improve the game.

1. Keep `STEP_CYCLES=280896` and the maximum four steps per draw. Reevaluate
   controls and boost before each executed step. Preserve the fractional
   remainder; under overload discard only complete-step backlog. Preserve
   current speed/air-slow-motion options and unsigned timer-wrap behavior.
2. Widen only arithmetic with demonstrated overflow, preserving ordinary
   results and existing lab ranges. Avoid wide division on the normal tyre
   path and measure the ARM cost. Do not disable deformation or detachments.
3. Derive wrong-way direction from horizontal velocity along the road, with
   the existing speed/duration thresholds and approximately 120-degree cone.
   At the goal, rank prior finishers ahead, then compare same-step crossing
   fractions without division. Exact simultaneous ties retain the current
   strict-ahead convention (the player receives the tied place).
4. Record race progress and road projection alongside the existing safe
   respawn point. An explicit `car_respawn` notification restores that earned
   position, clearing stale direction warnings. Keep completed checkpoints,
   laps, clocks and records monotonic, and retain the projection-jump guard
   for unrelated track lookups. This is runtime state, not a save change.

## Constraints

- Do not overwrite the source ROM or commit imported commercial assets.
- Keep the custom/original selection and record-bank identities unchanged.
- Keep `Car`, `Race` and `Rival` layouts unchanged. Two additional 32-bit
  values store safe race progress/projection; two retain the preceding Q8
  position for physical finish-plane interpolation. The latter pair is the
  review-driven correction for subunit ties and a real course-closing seam.
- No new framebuffer, heap allocation or large persistent array.
- Finish sorting may use a seven-element local snapshot of rival progress.
- Preserve the original native response profile hashes and reversible patcher.
- Publish to the existing draft PR; do not merge it automatically.

## Verification and limits

Observe focused failures before production changes. Compile and execute the
actual main-loop body and real physics/race code in host fixtures. Check
cadence, six control boundaries, real impact paths, reverse direction,
chronological finishes and recovery across checkpoint/start lines. Preserve
the existing suite, compile both GBA configurations, and check generated data.

Use normal-controller emulator runs for harder driving, braking/reversing,
nitro and complete original-course races. Repeat final inputs against the
same binary, inspect actual screenshots, and report rendering FPS and linked
RAM with the final hash. Native two-controller coverage may be extended
using ordinary input and read-only observation; make no claim beyond runs
actually completed. Physical hardware, full balance, larger custom maps and
replacement physics remain later work.
