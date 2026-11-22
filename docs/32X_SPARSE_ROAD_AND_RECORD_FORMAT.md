# Virtua Racing Deluxe 32X: verified road grids and record boundaries

**November 22, 2026. Source identification and error correction.** This work
uses the user's 3,145,728-byte US 32X cartridge, unchanged, with SHA-256
`cc2917895842ba42ce871a07a327c5467a5b5df8b7999ccc4b7b5bea9d4caf6b`
and MD5 `72b1ad0f949f68da7d0a6339ecd51a3f`.

## Critical corrected mapping

The previous research incorrectly subtracted `$880000` from pointers to
`$94xxxx`, `$97xxxx`, etc. That mapping accesses unrelated bytes and returns
spurious large unique counts; it is **not** correct for 32X road data.

There are separate code and data windows in this cartridge:

| Logical item | 68000 address | Physical ROM-file offset |
|---|---|---|
| 68K road lookup *code* | `$8873E8` | `$0073E8` |
| Normal road grid 0 | `$94C000` | `$14C000` |
| Normal tile data 0 | `$970000` | `$170000` |
| Track auxiliary data pointer table | `$930612` | `$130612` |

The **road data** translation is `physical = 68000_pointer - $800000`.
Read-only scripts restrict this to `$900000..$9FFFFF` and reject unknown
windows instead of silently aliasing. The 68K executable's small fixed
window uses a different translation and is not passed to the data helper.

The corrected arrays contain recognizable sparse track outlines, with
far-away background cells sharing a single dominant index. They can be
visualized from the verified user's source file; the repository does not
contain their raw data.

## Actual ROM-derived structure

Six 64×64 main lookup tables plus one alternate table are present at the
real cartridge offsets in this table:

| Slot | Main grid file offset | Record bank file offset | Distinct indexes | Non-background cells | Connected groups |
|---:|---:|---:|---:|---:|---:|
| 0 | `$14C000` | `$170000` | 295 | 294 | 1 |
| 1 | `$14E000` | `$188000` | 335 | 334 | 1 |
| 2 | `$150000` | `$1A0000` | 317 | 316 | 1 |
| 3 | `$154000` | `$1B8000` | 254 | 253 | 1 |
| 4 | `$158000` | `$1D0000` | 559 | 558 | 1 |
| 5 | `$152000` | `$1D0000` | 119 | 118 | 1 |
| Alt 3 | `$156000` | `$1C4110` | 16 | 15 | 1 |

These counts were checked against the supplied ROM; the old values
(1,460/1,237/1,549/1,670/1,580/1,526 and 1,569) were incorrect.
All 7×4,096 **28,672** lookup references resolve to physical data words
inside the cartridge; **in-bounds addresses alone do not decode geometry**.

### Count-delimited variable-length records

Each non-background reference in main slots 0–4 and alternate slot 3
points to the beginning of a record in a bank of big-endian 16-bit words.
For every one of these **1,770** records, the first word `N` exactly
predicts the next record boundary:

```
next_start = current_start + 15 + 14 * N   # word offsets, not bytes
subrecords_in_record = N + 1              # 14 words / 28 bytes each
```

All six streams begin at word offset zero, have no duplicate non-background
start indexes, and terminate at a `0xFFFF` sentinel. The record chains
contain **13,096** individual 14-word subrecords. The count formula has
**zero mismatches** in the entire verified five-slot-plus-alternate corpus.
The 14-word subrecord fields remain *unlabeled*. In particular, **we
cannot yet claim 3D vertices, height fields, road width, bank angle, pit
service or tunnel collision polygons have been decoded**.

The sixth normal lookup slot (5) is a different case. It shares main
slot 4's record bank but **does not** point to starts following this
counted-record format. It may be a special display/route selector, but its
actual purpose is **unverified**. The importer must not parse it as if it
were normal geometry.

### Additional four-page input table

The 68K's `scene_camera_init` code at file `$CCDE` loads pointer table
`$930612`, which maps to file `$130612`. Its first three entries point to
file `$13061E`, `$13261E`, `$13461E`. A separate
`track_data_extract_033` function reads **two signed bytes from each
of four pages, spaced `$800` bytes apart**, into a RAM work buffer.
Those ROM tables and page bounds were independently checked. It is still
unknown how they correspond to the five named Deluxe courses.

## Verification and limitations

- `python3 tools/inspect_vrd_32x_tables.py <owned-32x-rom>` validates exact
  full-file hash, both pointer tables, all word addresses, active-cell
  boundaries, and auxiliary page pointers.
- `python3 tools/inspect_vrd_32x_records.py <owned-32x-rom>` checks each
  counted record and the exceptional sixth slot. No original data is
  written by default; JSON reports contain only counts and offsets.
- Source-only regression fixtures detect the earlier wrong bank mapping,
  arbitrary-pointer corruption, count-header mutation and sentinel mutation.
  User-private tests additionally assert exact 32X ROM sizes, hashes and
  all real record boundaries.
- **No 32X course has yet been imported as playable 3D track geometry**.
  The five track names and slot assignment, per-record field meanings,
  branching-road traversal, polygon models and performance budgets all
  require independent runtime disassembly/emulator confirmation.
- No Race Drivin' Genesis or Atari arcade physics code was transplanted in
  this checkpoint. That remains a separate research milestone.

The mapping and 68000 function descriptions were independently checked
against [the published Virtua Racing Deluxe 32X disassembly](https://github.com/matiaszanolli/sega-vr-disasm),
particularly `analysis/TRACK_DATA_FORMAT.md` and
`disasm/modules/68k/game/camera/scene_camera_init.asm`.
