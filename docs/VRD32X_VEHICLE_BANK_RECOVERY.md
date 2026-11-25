# Original 32X vehicle bank recovery

The source cartridge is the exact US 32X Deluxe revision verified by SHA-256 in the importer. The donor and generated geometry remain private; this branch contains source and synthetic fixtures only.

## Verified
- SH-2 copy routine at ROM 0x24664–0x24692; the table at 0x246A8 includes an empty entry followed by four source ranges. Its first three real ranges provide the active car-type banks, and the fourth contains the common display-selection variants. All are copied to SH-2 0x0603C000. Identical SDRAM pointers in different banks are **not** identical models.
- Player table 0x156C8, display table 0x157A0, opponent axles 0x158B4, opponent body table 0x158E4. Three types, four paint variants each, near/middle/far LODs. The source record setup at 0xC980–0xC9AE copies model pointers and local positions. Position records in the 0x9xxxxx window use the 0x800000 base. Local attachment translation and model vertex coordinates have different fixed-point shifts.
- Actual owner-ROM extraction: Formula 145 body vertices / 89 faces / 33 bank-qualified meshes; Stock 100 / 110 / 31; Prototype 122 / 103 / 31. Total 95, including 36 opponent LOD/color entries. Every mesh was reconstructed from 48-vertex batches with the same ordered face vertices and unchanged raw material and normal metadata.
- Five focused tests passed locally with the private donor (four source-only fixtures and one exact-ROM check). The exact-ROM check is skipped unless explicitly enabled with VRD32X_ROM.

## Reproduce privately

    python tools/export_vrd32x_vehicle_models.py --rom "/path/to/Virtua Racing Deluxe (USA).32x"
    VRD32X_ROM="/path/to/Virtua Racing Deluxe (USA).32x" python -m unittest discover -s tests -p test_vrd32x_vehicle_models.py -v

Output defaults to ignored build/vrd32x-vehicles/models.json. JSON contains source offsets, bank-qualified model keys, unscaled signed coordinates, ordered faces, raw color and normal data, the original LOD lookup, and 48-vertex batches. Uninterpreted player/display fields are retained rather than guessed.

## Remaining integration

This is a source-geometry importer, not a rebuilt or emulator-verified GBA game. RGB palette translation, wheel rotation and suspension, collision alignment, runtime quantization, renderer and frame timing still require verification. The most recent private build was not available in this session, so this branch intentionally leaves gameplay, saves, and existing ROM releases untouched. Integrate into the current private source when available rather than reverting the later build.
