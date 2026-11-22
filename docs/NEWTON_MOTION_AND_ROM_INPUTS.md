# Independent Newton motion and verified new cartridge inputs

November 22, 2026. No Tracin' 240x160 GBA engine, existing optional
Genesis/SVP import retained. User-supplied ROMs were inspected privately;
**no proprietary game data or derivative imported headers are committed**.

## User intent and actual two-axis selector

**HANDLING** remains a four-way independent option: Soft-Body, Arcade,
Hard Style, Race Style. It governs steering, grip, and yaw. The new
**MOTION PHYS** option is Original or Newton and affects loop contact,
falling and landing. A user can select *any handling profile* with either
motion model. The default is Original to preserve previous controls.

Legacy NTR2 SRAM has exactly 16 option bytes. Handling remains `opt[14]`;
motion uses previously reserved `opt[15]` (older saves set this to zero).
The save struct, magic, records, Car/Race layouts and imported pack identity
are unchanged. The menu now scrolls cleanly at 240x160 instead of clipping
the final options off-screen.

## What Newton mode physically does

The existing 60 Hz game simulation already has gravity from the Physics
Lab and momentum in the air. The corrected *loop fall* rule now evaluates
the inward support acceleration of a vertical loop:

`N/m = v^2/R + g * cos(theta)`.

When this is negative, the loop can no longer push the car inward; in
Newton mode it detaches at its current point with its existing tangential
velocity. There is **no forced crash, arbitrary extra downward kick or
teleport**. It then follows ordinary ballistic gravity. A fast car with
positive support stays on the loop; normal loop completion is unchanged.
Original motion retains the legacy scripted falling behavior.

For the simple/Arcade car model, landings use a positive impulse along
the ground plane normal `(-gx, 256, -gz)`. Hard impacts return 25% of the
incoming normal speed; the normal impulse changes both vertical and
horizontal momentum on a slope without artificially erasing the tangential
component. The deformable Soft-Body model already handles contacts and
suspension node-by-node and uses its own ground contact solver; it shares
the new loop-release behavior.

**This is a new GBA-native Newton-inspired model, not extracted original
Race Drivin' code.** Reverse-engineering exact equations from the Sega
68000 ROM remains a distinct research task.

## Exact supplied ROM identities, no data copied

| Source | Size | Sega product | Full SHA-256 | Header checksum |
|---|---:|---|---|---|
| Race Drivin' USA Genesis (1993) | 524,288 | GM T-48246 -00 | bd86c32fba134c64c9698d939b9d679ad93b77a2864019b63effad1be96002e5 | bd49 |
| Virtua Racing Deluxe US 32X (1994) | 3,145,728 | GM MK-84601-00 | cc2917895842ba42ce871a07a327c5467a5b5df8b7999ccc4b7b5bea9d4caf6b | 1e4d |

Both complete cartridge headers, 68000 reset vectors and 16-bit Sega
checksums matched the input data. The exact SHA-256 values are recorded
as immutable source IDs in the read-only
`tools/audit_uploaded_cartridges.py` tool. The original images were not
overwritten. The prior three-track importer still requires the verified
2,097,152-byte Genesis/SVP Virtua Racing image and **rejects** the new
3,145,728-byte 32X image. No unverified 32X level offsets are guessed.

The Sega Genesis Race Drivin' software is a 68000 console port; its
program is not a byte-for-byte copy of the Atari arcade TMS34010 physics
processor described in other research. We must establish actual routine
identity before claiming an authentic transplant.

## Tests and limits

Production C host tests cover Original/Newton loop roof contact, high-speed
retention, sustained soft-body gravity, slope-normal landing and unchanged
on-ground steering for the three non-soft-body handling modes, with shift,
signed-overflow and divide-by-zero sanitizers. They run in both standard
and original-course source configurations.

The mGBA CI probe also selects Handling and Motion Physics using ordinary
buttons, checks runtime global values and the existing SRAM option[15],
enters Arcade and drives without writing guest memory. Source-only CI does
not embed donor ROMs; optional original-course builds and actual uploaded
ROM tests run privately.

**Not yet demonstrated:** exact Genesis Race Drivin' physics-code
equivalence, physical Atari arcade simulation, decoded 32X Highland/Sand
Park level meshes, 32X vehicle classes, completed three-lap Newton races
or true hardware timing. The current Newton mode is an independently
selectable foundation, not the finished original physics port.

## How to use

On the game title: Start → Mode Select → Options.
Set HANDLING to any of the four options, then MOTION PHYS to ORIGINAL
or NEWTON, press B to save and return. Stunt City has the physical loop;
Arcade/Free Run keep the existing three custom circuits and optional
three original Sega Genesis/SVP circuits.

For source ROM verification (read-only, never commits assets):

```sh
python3 tools/audit_uploaded_cartridges.py --require-known \
  "/path/to/Race Drivin' (USA).md" \
  "/path/to/Virtua Racing Deluxe 32X (U).bin"
```
