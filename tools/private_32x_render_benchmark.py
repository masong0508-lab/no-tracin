#!/usr/bin/env python3
"""Reproduce 32X native-scene FPS A/B tests with controller-only mGBA.

mGBA 0.10.x portable core, verified library ABI from this project's own
headless release. No original Sega cartridge, generated assets or private
ROM files are embedded. Never writes the guest RAM. Compare display draws
per emulated GBA second; emulated video refresh is 59.7275 Hz.

Original 32X visual geometry is authenticated road source; the landscape
and scenery meshes are separate, explicitly GBA-authored approximations.
"""
import argparse
import ctypes as C
import json
from pathlib import Path

from private_mgba_all32x_smoke import Emulator,KEY_START,KEY_A,KEY_L,KEY_RIGHT

VIDEO_HZ = 59.7275


def sample_rom(rom, dll, frames, output):
    output.mkdir(exist_ok=True,parents=True)
    rows=[]
    for slot in range(5):
        emulator=Emulator(dll,rom)
        try:
            emulator.run(180)
            if emulator.errors:raise RuntimeError('mGBA startup error')
            emulator.run(12,KEY_START);emulator.run(60)
            emulator.run(12,KEY_A);emulator.run(80)
            emulator.tap(KEY_L)
            for _ in range(slot):emulator.tap(KEY_RIGHT)
            emulator.run(12,KEY_A);emulator.run(45)
            actual=emulator.read32(COURSE_ADDRESS+4)
            if actual!=3+slot:
                raise AssertionError(f'original source mode {slot} selected {actual} not {3+slot}')
            start=emulator.read32(DRAW_ADDRESS)
            previous=C.c_int32(emulator.read32(COURSE_ADDRESS+12)).value
            emulator.run(frames,KEY_A)
            draws=emulator.read32(DRAW_ADDRESS)-start
            progress=C.c_int32(emulator.read32(COURSE_ADDRESS+12)).value
            if draws<=15 or emulator.errors or progress<=previous:
                raise AssertionError(f'failed driving mode={slot}: draws={draws} '
                                     f'progress={previous}->{progress}, errors={emulator.errors}')
            emulator.capture(output/f'original-slot{slot}.png')
            rows.append(dict(slot=slot,video_frames=frames,game_draws=draws,
                             displayed_fps=round(draws*VIDEO_HZ/frames,4),
                             progress_delta=progress-previous,
                             emulator_errors=emulator.errors,
                             controller_only=True,guest_ram_writes=False))
        finally:
            emulator.close()
    return rows


def run(original,new,dll,out,draw_addr,race_addr,frames):
    global DRAW_ADDRESS, COURSE_ADDRESS
    DRAW_ADDRESS=draw_addr
    COURSE_ADDRESS=race_addr
    before=sample_rom(original,dll,frames,out/'before')
    after=sample_rom(new,dll,frames,out/'after')
    results=[]
    for b,a in zip(before,after):
        results.append(dict(course_index=b['slot'],before=b,after=a,
                            improvement_fps=round(a['displayed_fps']-b['displayed_fps'],4),
                            speedup_percent=round((a['game_draws']/b['game_draws']-1)*100,2)))
    (out/'comparison.json').write_text(json.dumps(results,indent=2)+'\n')
    return results


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--before',type=Path,required=True)
    p.add_argument('--after',type=Path,required=True)
    p.add_argument('--lib',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--draw-address',type=lambda x:int(x,0),required=True)
    p.add_argument('--race-address',type=lambda x:int(x,0),required=True)
    p.add_argument('--frames',type=int,default=760)
    args=p.parse_args()
    if args.frames < 100 or args.frames > 4000:
        p.error('Expected 100..4000 emulated video frames per mode')
    for row in run(args.before,args.after,args.lib,args.output,
                   args.draw_address,args.race_address,args.frames):
        print(f"Original course {row['course_index']}: "
              f"{row['before']['displayed_fps']} -> {row['after']['displayed_fps']} "
              f"FPS, {row['speedup_percent']:+.1f}%")
