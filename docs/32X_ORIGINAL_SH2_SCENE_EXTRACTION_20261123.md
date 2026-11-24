# Original Deluxe display geometry: replacement path, not another road study

The owner explicitly requests full replicas of the five Virtua Racing Deluxe (USA) 32X courses: Big Forest, Bay Bridge, Acropolis, Highland and Sand Park. The existing 68000 collision/route data is useful but the authored offroad terrain and roadside props are not replicas. Do not spend the next map pass optimizing or embellishing those proxies.

A public independent HLE renderer documents the actual SH-2 model format and source tables: [VirtuaRacingDeluxeEnhanced, hle.c, pinned revision](https://github.com/PokaMilkTea/VirtuaRacingDeluxeEnhanced/blob/99b5ff98473d958b4e6d3d987bce0ec16c57a869/src/hle.c). The new independent Python extraction module reads the five 32x32 model-pointer grids at file offsets 0x15C000, 0x15D000, 0x15E000, 0x15F000 and 0x161000, traverses the actual model vertices and triangle/quad strips, retains header, material index and normal words, and deduplicates static meshes. It additionally reads original gate/checkpoint landmark entries and documented course-specific prop references, including the Big Forest Ferris wheel and pirate ship. Local model vertices and animation records remain separate until their transform is verified.

## Private extraction

From the source checkout, with the owner's unmodified USA donor ROM:

    python3 tools/export_vrd32x_sh2_scene.py --rom "/private/Virtua Racing Deluxe (USA).32x"
    python3 -m unittest discover -s tests -p test_vrd32x_sh2_scene.py -v

The default output is the git-ignored build/vrd32x-sh2-scene directory. Five JSON scene dumps contain source geometry, cell indices, materials, normals and landmark records; five OBJ exports contain the static world-space meshes, not guessed landscape. No RGB palette or animated prop transformation is claimed. Never add generated owner-only files or the donor to public GitHub.

## Verification and next integration

Four synthetic source-only unit tests passed locally: triangle/quad strips and normal records, grid/landmark extraction and deduplication, malformed pointers/truncation/indexes, uninitialized strips and empty grids. The original cartridge's Library entry was located but its raw bytes were not authorized for materialization during this session. Thus the new extractor has NOT been run against the donor, and no new GBA graphics, screenshots, FPS measurement or complete maps are claimed.

Once actual donor extraction can be run, verify all five exported meshes against original 32X views, recover actual RGB palettes, verify coordinate/height alignment against the already decoded collision road and the Highland two-layer bridge, place transformed original gate/landmark meshes, and integrate ROM-only cell-indexed scene rendering with bounded per-frame polygons. Use the GBA engine's existing framebuffer and preserve collision and save behavior. Real-emulator matched positions and full lap comparison are required before labeling a course a replica. Do not treat an export-only milestone or another placeholder ROM as completion.
