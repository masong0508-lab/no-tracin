CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
CFLAGS  := -mcpu=arm7tdmi -mthumb -mthumb-interwork -O2 -Wall -Werror=shift-negative-value -ffreestanding -fno-builtin
LDFLAGS := -mcpu=arm7tdmi -mthumb-interwork -nostartfiles -nostdlib -T gba.ld -Wl,--gc-sections
OBJS    := build/crt0.o build/fill.o build/main.o build/render.o build/world.o build/car.o build/hud.o build/fx.o build/sound.o build/game.o build/menu.o build/track.o build/race.o build/softbody.o build/props.o build/edit.o build/tune.o build/libc.o

notracin.gba: build/notracin.elf
	$(OBJCOPY) -O binary $< $@
	python3 tools/fixheader.py $@

build/notracin.elf: $(OBJS) gba.ld
	$(CC) $(LDFLAGS) $(OBJS) -lgcc -o $@

build/crt0.o: src/crt0.s | build
	$(CC) -mcpu=arm7tdmi -c $< -o $@

build/fill.o: src/fill.s | build
	$(CC) -mcpu=arm7tdmi -c $< -o $@

build/%.o: src/%.c src/gba.h src/world.h src/car.h src/hud.h src/fx.h src/sound.h src/game.h src/menu.h src/track.h src/race.h src/softbody.h src/props.h src/edit.h src/tune.h src/tracks_data.h src/tracks_dims.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/render.o: src/sintab.h
build/hud.o: src/font.h
build/menu.o: src/font.h

build:
	mkdir -p build

clean:
	rm -rf build notracin.gba racedrivin.gba

# Optional local import. Its generated data and all objects stay separate from
# the normal build, so changing packs cannot accidentally reuse stale objects.
VR_BUILD := build/vr-original
VR_OBJS := $(patsubst build/%.o,$(VR_BUILD)/%.o,$(OBJS)) $(VR_BUILD)/vr_original.o
VR_FLAGS := $(CFLAGS) -DWITH_VR_ORIGINAL_TRACKS -I$(VR_BUILD)
VR_HEADERS := $(VR_BUILD)/vr_original_tracks.h $(VR_BUILD)/vr_original_mesh.h

.PHONY: vr-original test
vr-original:
	@test -n "$(VR_ROM)" || (echo 'Supply VR_ROM="/path/to/Virtua Racing (USA).md"' >&2; exit 2)
	python3 tools/import_vr_original.py --rom "$(VR_ROM)" --output-dir $(VR_BUILD)
	$(MAKE) $(VR_BUILD)/notracin-vr-original.gba

$(VR_BUILD)/notracin-vr-original.gba: $(VR_BUILD)/notracin.elf
	$(OBJCOPY) -O binary $< $@
	python3 tools/fixheader.py $@

$(VR_BUILD)/notracin.elf: $(VR_OBJS) gba.ld
	$(CC) $(LDFLAGS) $(VR_OBJS) -lgcc -o $@

$(VR_BUILD)/crt0.o: src/crt0.s | $(VR_BUILD)
	$(CC) -mcpu=arm7tdmi -c $< -o $@

$(VR_BUILD)/fill.o: src/fill.s | $(VR_BUILD)
	$(CC) -mcpu=arm7tdmi -c $< -o $@

$(VR_BUILD)/%.o: src/%.c $(wildcard src/*.h) $(VR_HEADERS) | $(VR_BUILD)
	$(CC) $(VR_FLAGS) -c $< -o $@

$(VR_BUILD):
	mkdir -p $@

test:
	python3 -m unittest discover -s tests -p 'test_*.py' -v

build/track.o: src/tracks_data.h

src/tracks_data.h src/tracks_dims.h &: tools/mktracks.py
	python3 tools/mktracks.py

# PRIVATE 32X road-study pack. The US 32X source ROM never enters Git.
# Adds five decoded, sampled road-elevation environments as Free Run studies;
# no asserted Sega course names, lap/finish, scenery or original mesh parity.
VRD_BUILD := build/vr-deluxe-study
VRD_OBJS := $(patsubst build/%.o,$(VRD_BUILD)/%.o,$(OBJS)) $(VRD_BUILD)/vr_original.o $(VRD_BUILD)/vrd32x_study.o
VRD_FLAGS := $(CFLAGS) -DWITH_VR_ORIGINAL_TRACKS -DWITH_VRD_32X_TRACKS -I$(VRD_BUILD)
VRD_HEADERS := $(VRD_BUILD)/vr_original_tracks.h $(VRD_BUILD)/vr_original_mesh.h $(VRD_BUILD)/vrd32x_study_tracks.h $(VRD_BUILD)/vrd32x_study_data.h

.PHONY: vr-deluxe-study
vr-deluxe-study:
	@test -n "$(VR_ROM)" -a -n "$(VR32X_ROM)" || (echo 'Supply VR_ROM=/path/to/Genesis-Virtua.bin VR32X_ROM=/path/to/Deluxe32X.bin' >&2; exit 2)
	python3 tools/import_vr_original.py --rom "$(VR_ROM)" --output-dir $(VRD_BUILD)
	python3 tools/import_vrd_32x_explorer.py --rom "$(VR32X_ROM)" --out-dir $(VRD_BUILD)
	$(MAKE) $(VRD_BUILD)/notracin-vr-deluxe-study.gba

$(VRD_BUILD)/notracin-vr-deluxe-study.gba: $(VRD_BUILD)/notracin.elf
	$(OBJCOPY) -O binary $< $@
	python3 tools/fixheader.py $@

$(VRD_BUILD)/notracin.elf: $(VRD_OBJS) gba.ld
	$(CC) $(LDFLAGS) $(VRD_OBJS) -lgcc -o $@

$(VRD_BUILD)/crt0.o: src/crt0.s | $(VRD_BUILD)
	$(CC) -mcpu=arm7tdmi -c $< -o $@

$(VRD_BUILD)/fill.o: src/fill.s | $(VRD_BUILD)
	$(CC) -mcpu=arm7tdmi -c $< -o $@

$(VRD_BUILD)/%.o: src/%.c $(wildcard src/*.h) $(VRD_HEADERS) | $(VRD_BUILD)
	$(CC) $(VRD_FLAGS) -c $< -o $@

$(VRD_BUILD):
	mkdir -p $@

# Source-authenticated 32X collision boundary evidence; NOT a playable
# named-course import or SH-2 scenery mesh reconstruction. Files stay private
# under ignored build/ and are generated from the owner's unmodified ROM.
.PHONY: vr32x-original-bsp-evidence
vr32x-original-bsp-evidence:
	@test -n "$(VR32X_ROM)" || (echo 'Supply VR32X_ROM="/path/to/Virtua Racing Deluxe 32X (U).bin"' >&2; exit 2)
	python3 tools/export_vrd32x_exact_bsp.py --rom "$(VR32X_ROM)" --out-dir build/vrd32x-bsp
