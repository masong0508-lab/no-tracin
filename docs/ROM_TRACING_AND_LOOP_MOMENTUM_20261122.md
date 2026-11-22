# Verified 32X map indexes, Race Drivin' 68000 audit and Newton loop exits

Checkpoint: November 22, 2026. No original game bytes or generated game
assets are committed. Both ROMs were inspected read-only against exact
whole-file hashes.

## Virtua Racing Deluxe 32X: actual ROM lookup tables

The user's 3,145,728-byte US 32X ROM has SHA256
\`cc2917895842ba42ce871a07a327c5467a5b5df8b7999ccc4b7b5bea9d4caf6b\`
and MD5 \`72b1ad0f949f68da7d0a6339ecd51a3f\`. This **matches the
original image documented by Matias Zanolli's complete 32X disassembly**:
https://github.com/matiaszanolli/sega-vr-disasm .

Its analysis of \`track_data_index_calc_table_lookup\` identifies the 68000
function at file \`$0073E8-$00742C\` and two pointer tables:
normal at \`$00742C\`, alternate at \`$00745C\`. Actual cartridge bytes
confirm **six normal 64x64 grids and one alternate grid**, each storing
big-endian unsigned 16-bit indices. The 68000's ROM base is \`$880000\`.
A map word identifies a tile-data address at \`base_file_offset + word*2\`.

| Normal slot | Map file offset | Base tile-data offset | Distinct references |
|---:|---|---|---:|
| 0 | \`$0CC000\` | \`$0F0000\` | 1,460 |
| 1 | \`$0CE000\` | \`$108000\` | 1,237 |
| 2 | \`$0D0000\` | \`$120000\` | 1,549 |
| 3 | \`$0D4000\` | \`$138000\` | 1,670 |
| 4 | \`$0D8000\` | \`$150000\` | 1,580 |
| 5 | \`$0D2000\` | \`$150000\` | 1,526 |

The only populated alternate slot is 3: map file \`$0D6000\`,
base tile-data \`$144110\`, 1,569 distinct references.
All **7 × 4096 = 28,672 references** resolve to valid addresses
inside the supplied 3 MB ROM. The read-only
\`tools/inspect_vrd_32x_tables.py\` reproduces all offsets and counts
and refuses unsupported source hashes.

**Important:** Six table *slots* do not prove six distinct courses or
establish the order of the five Deluxe tracks. These are decoded **lookup
indices**, not 3D geometry, collision meshes, Sand Park branches, or
complete Highland roads. Future import work must verify the named
track-to-slot mapping, segment graph and geometry against original 32X
gameplay and SH-2 rendering before importing assets.

Original technical reference:
https://github.com/matiaszanolli/sega-vr-disasm/blob/master/analysis/TRACK_DATA_FORMAT.md

## Race Drivin' Genesis: conservative 68000 call candidates

The supplied 524,288-byte US Genesis Race Drivin' image is SHA256
\`bd86c32fba134c64c9698d939b9d679ad93b77a2864019b63effad1be96002e5\`.
Its reset vector points to file \`$200\`. A raw even-address scan of
\`JSR abs.l\` words found **920 plausible call references to 147 target
addresses**. Dense windows: \`$68000-$6BFFF\` (449),
\`$6C000-$6FFFF\` (224), \`$70000-$73FFF\` (158),
\`$74000-$77FFF\` (52). These windows also have signed multiply-word
patterns (34, 26, 54, 43, respectively).

**These are raw patterns, NOT verified executed routines or proven gravity
formulas**. Mixed code and data can produce false positives. Identifying
the actual gravity and suspension model requires disassembly of reachable
paths and runtime 68000 traces of velocity/position writes. The Genesis
port is not automatically identical to the Atari arcade TMS34010 program.

\`tools/trace_racedrivin_genesis.py\` emits a small *candidate-only* JSON
report without storing cartridge bytes.

## Newton mode: continuous momentum on loop completion and rollback

The existing GBA loop advances horizontally by \`shift\` over its
circumference. The older code zeroed X velocity and snapped X to the loop
endpoint upon completion or backward exit. With independently selected
**MOTION PHYS: NEWTON**, the new exit carries:

\`\`\`
vx = loop_velocity * shift * 256 / loop_length
x += residual_distance * shift * 256 / loop_length
heading = atan2(shift, loop_length / 256)
\`\`\`

The reverse exit uses the same signed tangent velocity. The original
**MOTION PHYS: ORIGINAL** still returns zero lateral speed and its previous
fixed endpoint. Regressions execute the production \`car_step\` under three
handling models and exercise both directions; the existing upside-down
ballistic release and sloped landing checks continue to pass.

Conversions like \`loop.z << 8\` with a negative coordinate were also
replaced by \`loop.z * 256\`, eliminating a reproduced signed-shift
sanitizer failure. No extra framebuffers, heap allocations or persistent
save fields were added.

## Validation and limits

Both standard and user-imported original-course GBA editions compile with
GNU ARM EABI GCC. New host and read-only ROM tests passed locally using
both supplied cartridges. The full source-only GitHub suite and real mGBA
controller smoke tests passed after the changes. The new ROM builds booted
and drove on the standard course and the three optional Genesis-derived
Virtua Racing courses under Newton motion with zero mGBA errors.

These are short functional checks—not full three-lap Arcade finishes,
physical-hardware timing benchmarks or proof of identical Atari physics.
There is still no verified 32X Deluxe course geometry importer. The next
milestone is to trace the actual 32X grid-to-geometry/route pointers and
cross-validate Race Drivin' dynamics against real 68000 execution.
