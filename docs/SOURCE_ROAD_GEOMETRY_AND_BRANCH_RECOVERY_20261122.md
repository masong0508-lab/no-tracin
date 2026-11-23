# Original-map fidelity continuation: native side roads and sampled 32X silhouettes

**Checkpoint: November 22, 2026 (local time).** Continuation of
[the first source-mask map pass](ORIGINAL_MAP_FIDELITY_20261122.md).
Part of stacked, still-draft No Tracin' pull request #3.
No user-provided proprietary ROMs, polygon arrays or private generated
headers are committed to this source branch.

## Genesis/SVP originals: side roads do not award phantom racing progress

The three original-course pack already contains ROM-decoded main-road nodes,
packed source asphalt and original scenery meshes. Source asphalt often
extends beyond the width of that main route (for example, pit/branch roads).
Previously `race_step` still projected the car onto a far-away main-route
segment whenever the coarse route grid offered a candidate. That can
invent unrelated main-route progress, a WRONG WAY message pointing against
the pit lane, and a recovery pose on the central spline.

On the imported **Genesis original** tracks only, an on-asphalt pose
farther than the sampled main-route half-width is now treated as
**native side-road exploration**:
- No credit is awarded through an unrelated main-route projection.
- No WRONG WAY warning is generated from an unknown pit-lane tangent.
- Every 16 physics ticks (and on ground), a recovery pose is stored at the
  *actual source asphalt* road position if four surrounding footprints at
  16 GBA units also classify as source asphalt. The car's original heading
  and the last earned legitimate main-route progress are preserved.
- Rejoining the verified main circuit resumes existing checkpoint/lap
  progression, records, road-direction logic and opponent behavior.
- Custom tracks and the prior 32X free drive branch remain separate.

This **does not** implement original pit-lane lap progress or race marshal
rules. The original Genesis `road_nodes` graph includes alternate
connections that must be validated and integrated for full competitive
pit behavior; guessing route IDs from the closest segment is not acceptable.

## 32X source-mask map previews: tightly framed, proportional road coverage

The existing private 32X importer samples the real 68000 BSP collision/height
plane on a 512×512 road grid. The old preview mapped every nonempty 64×64
lookup tile to a rectangular blot; the preceding checkpoint improved
coverage to 4×4 sample quadrants. The latest implementation goes further:
- A new **stdlib-only** `sampled_road_bounds()` derives precise inclusive
  pixel-sample extents, with a small clamped margin, from source-valid
  points. That function contains no cartridge data.
- Private importer emits a ROM-constant `u16 [5][4]` bounds table
  (40 bytes in an optional generated header, not EWRAM).
- Each map pixel is projected back into the real source-sampled road mask,
  with GBA-world +Z upward and original aspect ratio preserved.
- A centre and four small subpixel probes prevent thin **source-supported**
  roads from disappearing in downsampled thumbnails. This is at most
  thumbnail-pixel-level antialiasing, not a fabricated full-tile road.
- The approximation only improves visual silhouette display: 32X
  source-derived *staging cycles* are **not** drawn as true race routes.

The sparse road-mask and height field are not original SH-2 scenery mesh
data, and the vertical scale remains unverified. Importer stats record
`preview_sampled_road_bounds_gba`; they do not claim named course identity.
No new runtime framebuffer, heap, polygon list or large RAM cache is added.

## Verification and release gates

Synthetic source-only tests verify:
1. Exact source-mask rectangular extents with margins, clipping at world
   edges, rejection of corrupt/empty masks and thin one-cell branches.
2. Compiled production-C 32X map projection preserves an intentionally
   missing lower-left road quadrant without hiding the other three,
   and rejects unknown mode slots.
3. Compiled production-C original-circuit recovery distinguishes source
   side-road position from an unrelated main-route projection, keeps
   earned lap state, clears invalid wrong-way state and restores the
   genuine safe source-road pose after leaving it.

```sh
make test
make -j2
```

**Still outstanding before claiming original Deluxe races:** match ROM
lookup modes to actual named course selections by 68000/SH-2 runtime
tracing; reconstruct original SH-2 graphics, start/finish, pit branches,
and racing path graph; compare captured silhouettes and geometry against
32X emulator frames; confirm complete laps and FPS in a donor-backed
private GBA build and on physical hardware. Source-only CI cannot
prove private-ROM parity, and it cannot build an asset-bearing release.
