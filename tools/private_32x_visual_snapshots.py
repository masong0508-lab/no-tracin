#!/usr/bin/env python3
"""Read-only early road/landscape captures for owner-local mGBA visual QA."""
from pathlib import Path
import ctypes as C
from private_mgba_all32x_smoke import Emulator, KEY_A, KEY_START, KEY_L, KEY_RIGHT
from PIL import Image, ImageOps, ImageDraw

def capture(rom, lib, output):
    output=Path(output);output.mkdir(exist_ok=True,parents=True)
    images=[]
    for slot in range(5):
        e=Emulator(Path(lib),Path(rom))
        try:
            e.run(180)
            e.run(12,KEY_START);e.run(60)
            e.run(12,KEY_A);e.run(80)
            e.tap(KEY_L)
            for i in range(slot):e.tap(KEY_RIGHT)
            e.run(12,KEY_A);e.run(45)
            e.capture(output/f'{slot}-starting.png')
            e.run(150,KEY_A)
            e.capture(output/f'{slot}-straight.png')
            for frame in ('starting','straight'):
                images.append(Image.open(output/f'{slot}-{frame}.png'))
        finally:
            e.close()
    sheet=Image.new('RGB',(240*5,320),(8,15,26))
    for slot in range(5):
        sheet.paste(images[slot*2],(240*slot,0))
        sheet.paste(images[slot*2+1],(240*slot,160))
    sheet.save(output/'all-five-early-driving.png')
    return sheet

if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('--rom',required=True)
    p.add_argument('--lib',required=True);p.add_argument('--out',required=True)
    a=p.parse_args();capture(a.rom,a.lib,a.out)
