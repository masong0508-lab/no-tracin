# Native controls and race traffic continuation

## Intent and authorization

The user asked to proceed after the previous milestone proposed independently
adjustable steering and drive response in the original Mega Drive engine.
Continue that work and strengthen actual GBA race validation. Original maps
remain optional in No Tracin'. Preserve the supplied image, save compatibility,
and the measured rendering improvements.

## Approach and implementation decisions

The existing native patch changes the final speed-response divisor, while a
separate signed table controls the original digital steering target ramp.
Expose the two independently through the existing strict Python patcher.
The builder selects bounded, verified settings: drive divisors 225 and 180;
steering steps 32 and 48. Default invocation remains exactly the earlier
180/32 output. The original steering cap, deadbands, release and averaging
instructions remain intact.

Alternatives considered: unrestricted numeric tuning would produce untested
images; rewriting the steering routine would add new timing and mode risks.
Neither is needed for independent control. A table-only step16 experiment
proved asymmetric because of an existing target deadband. It is excluded;
adding a matching deadband patch is separate future work.

On GBA, keep total race progress for ranking and rubber banding, but use
circular course distance for physical traffic interactions. Actual production
code reproduces missed collision/following/slipstream when only a nearby
rival's lap count changes. The intended slipstream also needs to reach the
persistent soft-body velocity: changing only the exported Car velocity is
discarded by the next soft-body update. Fix these demonstrated defects while
preserving the existing contact model and performance budget.

## Native interfaces and behavior

- Extend `transform(data, restore=False, *, drive_divisor=180,
  steering_step=32)` and `ips_bytes(restore=False, *, drive_divisor=180,
  steering_step=32)` in `tools/patch_vr_response.py`.
- Add `identify_profile(data)` returning `(drive_divisor, steering_step)` only
  for one of four exact supported image hashes.
- Apply accepts the exact original US image only. Restore detects any of the
  three modified profiles and reconstructs the exact original. Restoring an
  original image, patch stacking, unsupported values and modified revisions fail.
- CLI adds `--drive-divisor {180,225}` and `--steering-step {32,48}`.
  Restore rejects explicit selection flags, since it identifies the input.
- All outputs must be new and distinct from the source and each other.
  Ordinary IPS output describes the selected profile and correct direction.
- Preserve `ORIGINAL_SHA256`, `PATCHED_SHA256`, default `EDITS`, and legacy
  positional arguments for existing users and research scripts.

## GBA behavior and ownership

- `src/race.c`: wrapped physical gaps for nearby traffic; total progress still
  governs rank, race rubber band, checkpoints and finish.
- `src/softbody.c/.h`: a small external horizontal velocity addition updates
  the owner Car and previous positions of attached nodes, preserving shape,
  angular velocity and detached parts. Reject non-owner calls safely.
- Host tests exercise same physical scenes across lap offsets and real
  soft-body persistence; actual emulator tests measure faster laps, full
  three-lap Arcade races and the changed interaction when feasible.
- No new heap allocation or framebuffer. No new commercial asset in Git.

## Acceptance and limits

Native output hashes/checksums, exact edit sites, independent IPS application
and restoration must agree for all four profiles. Execute original68000
steering and speed routines, plus ordinary controller course runs. Document
any downstream steering state changed by the ramp; do not claim a new speed
cap or physical-hardware validation.

GBA changes must pass stock/optional host regressions and compiled gameplay
checks, with measured FPS and memory. Full-race completion is separate from
winning, competitive balance, or maximum-speed validation. Preserve previous
evidence rather than relabeling it as new tests.

Deliver the validated builds, profiles, instructions and evidence. Update the
existing draft PR; merging and a complete larger-map engine are outside this
pass.
