CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
CFLAGS  := -mcpu=arm7tdmi -mthumb -mthumb-interwork -O2 -Wall -ffreestanding -fno-builtin
LDFLAGS := -mcpu=arm7tdmi -mthumb-interwork -nostartfiles -nostdlib -T gba.ld -Wl,--gc-sections
OBJS    := build/crt0.o build/fill.o build/main.o build/render.o build/world.o build/car.o build/hud.o build/fx.o build/sound.o build/game.o build/menu.o build/track.o build/race.o build/softbody.o build/props.o build/edit.o build/libc.o

notracin.gba: build/notracin.elf
	$(OBJCOPY) -O binary $< $@
	python3 tools/fixheader.py $@

build/notracin.elf: $(OBJS) gba.ld
	$(CC) $(LDFLAGS) $(OBJS) -lgcc -o $@

build/crt0.o: src/crt0.s | build
	$(CC) -mcpu=arm7tdmi -c $< -o $@

build/fill.o: src/fill.s | build
	$(CC) -mcpu=arm7tdmi -c $< -o $@

build/%.o: src/%.c src/gba.h src/world.h src/car.h src/hud.h src/fx.h src/sound.h src/game.h src/menu.h src/track.h src/race.h src/softbody.h src/props.h src/edit.h src/tracks_data.h src/tracks_dims.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/render.o: src/sintab.h
build/hud.o: src/font.h
build/menu.o: src/font.h

build:
	mkdir -p build

clean:
	rm -rf build notracin.gba racedrivin.gba

build/track.o: src/tracks_data.h

src/tracks_data.h src/tracks_dims.h &: tools/mktracks.py
	python3 tools/mktracks.py
