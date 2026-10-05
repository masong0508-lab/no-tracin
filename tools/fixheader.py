#!/usr/bin/env python3
"""Set the GBA header complement checksum (bytes 0xA0-0xBC) and pad the ROM."""
import sys
path = sys.argv[1]
rom = bytearray(open(path, "rb").read())
chk = 0
for b in rom[0xA0:0xBD]:
    chk = (chk - b) & 0xFF
rom[0xBD] = (chk - 0x19) & 0xFF
while len(rom) % 4:
    rom.append(0)
open(path, "wb").write(rom)
print(f"{path}: {len(rom)} bytes, complement 0x{rom[0xBD]:02X}")
