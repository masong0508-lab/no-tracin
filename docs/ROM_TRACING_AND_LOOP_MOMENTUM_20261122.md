# Verified 32X map-index tables, Genesis 68000 audit, and Newton loop continuity

Checkpoint: November 22, 2026. All source-ROM inspection is read-only and
requires the user's own verified cartridges. **No proprietary ROM data is
committed to the No Tracin' repository.** The Genesis/SVP original course
importer, save structure and handling/motion selector are preserved.

## 1. Virtua Racing Deluxe 32X: real 68000 lookup structures

The user's **3,145,728-byte US 32X ROM** has SHA256
`cc2917895842ba42ce871a07a327c5467a5b5df8b7999ccc4b7b5bea9d4caf6b`
and MD5 `72b1ad0f949f68da7d0a6339ecd51a3f`, matching the published
complete buildable disassembly by Matias Zanolli:
https://github.com/matiaszanolli/sega-vr-disasm .

The published `analysis/TRACK_DATA_FORMAT.md` documents 68000
`track_data_index_calc_table_lookup` at ROM `$0073E8–$00742C`, with six
normal pointer pairs at `$00742C` and an alternate table at `$00745C`.
We verified the **actual bytes** at those offsets in the user's ROM.
**Correction to the earlier checkpoint:** the 32X road *data* pointers

e.g. `$0094C000` use the 68000 data window mapped to file offset
`address - $00800000`, **not** `address - $00880000`. The earlier report
mistakenly used the fixed code-window base for the data too and therefore
counted unrelated bytes as 2D lookup entries. Those earlier unique-value
counts and mapped offsets are **invalid and superseded below**. The
source-code lookup function at file `$0073E8` is in a different window.

The six correct 64×64 unsigned big-endian reference grids are sparse,
with one dominant background marker and a single connected region of
non-background tiles each. Corrected values:

| Normal slot | 68000 map ptr | True map file offset | True tile-data file base | Unique refs | Non-background cells |
|---:|---|---|---|---:|---:|
| 0 | `$0094C000` | `$14C000` | `$170000` | 295 | 294 |
| 1 | `$0094E000` | `$14E000` | `$188000` | 335 | 334 |
| 2 | `$00950000` | `$150000` | `$1A0000` | 317 | 316 |
| 3 | `$00954000` | `$154000` | `$1B8000` | 254 | 253 |
| 4 | `$00958000` | `$158000` | `$1D0000` | 559 | 558 |
| 5 | `$00952000` | `$152000` | `$1D0000` | 119 | 118 |

The populated **alternate slot 3** has map file `$156000`, data base
`$1C4110`, **16 distinct refs / 15 non-background cells**. All
**28,672 pointers** resolve inside the correct cartridge data regions;
this is now backed by a negative regression that rejects the earlier
wrong mapping. The six outlines can be reconstructed visually from the
actual ROM; there is no need to invent 2D map shapes.

### Verified variable-length records (new)

For normal slots **0–4** and alternate slot **3**, every one of the
**1,770** non-background cell records is bounded exactly by the formula

`next_record_word_offset = current_word_offset + 15 + 14 * first_word`.

All six streams terminate at a 16-bit `$FFFF` marker. This yields
**13,096 fixed-width 14-word (28-byte) subrecords** across the verified
streams. The high-level record boundaries are now decoded and tested,
but individual subrecord fields are not yet validated as 3D vertices,
road widths, heights or texture coordinates.

Normal slot **5** points into the same data bank as slot 4 but does **not**
follow this record-header grammar. It is kept as an unresolved variant,
not mislabeled as a sixth distinct track. Three further track-data base
pointers at ROM `$130612` have been verified as four-page `$800`-stride
signed-byte extraction inputs described in the published 68K disassembly;
those pages' physical meanings also need dynamic cross-checking.

**Important:** the six table *slots* are not proof of six different tracks
and their **names/order are not yet verified**. An alternate slot may
represent a route or game mode, rather than a full course. This is a
successfully decoded *lookup index*, not the original 3D road meshes,
height geometry, route graph, or Sand Park/Highland importer. Do not
publish a playable imported course without validating those next stages
against real 32X emulator traversal and the original game data format.

Tool and private command:

```sh
python3 tools/inspect_vrd_32x_tables.py \
  '/path/to/Virtua Racing Deluxe 32X (U).bin' --output 32x-grid-metadata.json
```

The JSON is statistics only, not copyrighted source tile arrays.

## 2. Race Drivin' Genesis: candidate code regions

The user's **524,288-byte US Genesis ROM** remains SHA256
`bd86c32fba134c64c9698d939b9d679ad93b77a2864019b63effad1be96002e5`.
The 68000 reset vector points to ROM offset `$200`. Scanning aligned ROM
words for plausible `JSR abs.l` instructions whose targets are inside the
cartridge found **920 candidate references to 147 unique targets**:

| ROM window | Raw plausible absolute JSRs | MULS.W word patterns |
|---|---:|---:|
| `$68000–$6BFFF` | 449 | 34 |
| `$6C000–$6FFFF` | 224 | 26 |
| `$70000–$73FFF` | 158 | 54 |
| `$74000–$77FFF` | 52 | 43 |

These are **raw opcode matches**, not verified executed instructions or
identified physics equations. A later step needs a Genesis 68000 emulator
trace of call sites, live velocity/position state and RAM writes to locate
which functions implement gravity, tire loading, springs and loop response.
The Sega Genesis port must not be conflated with the Atari arcade version's
TMS34010 car-physics processor.

Tool:

```sh
python3 tools/trace_racedrivin_genesis.py \
  "/path/to/Race Drivin' (USA).md" --output genesis-68k-candidates.json
```

## 3. Newton: continuous momentum through *both* loop exits

The original GBA loop geometry traverses a sideways displacement of
`shift` world units per loop. Legacy code forced horizontal X velocity
to zero on **full-loop completion or rollback**, creating a lateral snap.
For the independently selected **MOTION PHYS: NEWTON** mode, exit now keeps
`vx = v * shift * 256 / len`, advances X by the residual distance traveled
on that final physics tick, and aligns heading with the loop's tilted
exit tangent. The old **ORIGINAL** motion path remains unchanged.

The additional regression executes real production `car_step()` for all
three non-soft-body handling styles, in both forward and backward exit
conditions, and verifies velocity, heading and overstep position.
The previously unnoticed signed `l->z << 8` conversion also received the
arithmetic-equivalent `l->z * 256` fix; negative world coordinates are
common in the program's test fixtures. No heap or framebuffer allocation
was added. The existing loop-roof ballistic fall, slope-normal landing
and handling-independence tests still pass under shift/overflow sanitizers.

## 4. Bounded runtime validation and remaining work

The modified standard and optional GBA ROMs compile with GNU ARM GCC 14.2.1
and fit the original hardware's IWRAM/EWRAM regions. mGBA smoke tests
booted the new binaries, selected Newton independently via the actual
options menu and saved options byte 15, then drove on the custom circuit
and all three existing optional Genesis/SVP original circuits with zero
emulator errors. These are short controller-only sessions, not complete
races or hardware timing measurements. Real 32X geometry, port-specific
car classes, Race Drivin' original dynamics routines and Atari-equivalent
suspension remain future work.

Reference research, independently verified against the supplied ROM:
https://github.com/matiaszanolli/sega-vr-disasm/blob/master/analysis/TRACK_DATA_FORMAT.md
