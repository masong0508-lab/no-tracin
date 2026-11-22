#!/usr/bin/env python3
"""Read-only 68000 candidate-function inventory for Race Drivin' Genesis (USA).

Important: alignment/opcode scans on mixed program+data bytes cannot identify
physics routines by themselves. The output is an audit *candidate list*, not
an original code transplant or a disassembly of a full instruction stream.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

SOURCE_HASH = "bd86c32fba134c64c9698d939b9d679ad93b77a2864019b63effad1be96002e5"
SOURCE_BYTES = 524288
BANK_BYTES = 0x4000


def be16(data, offset):
    return int.from_bytes(data[offset:offset + 2], "big")


def be32(data, offset):
    return int.from_bytes(data[offset:offset + 4], "big")


def scan(data):
    """Find raw even-address absolute JSR/JMP and potential signed math words."""
    jsr_sources = Counter()
    jsr_targets = Counter()
    jump_sources = Counter()
    muls_sources = Counter()
    divs_sources = Counter()
    for offset in range(0x200, len(data) - 6, 2):
        opcode = be16(data, offset)
        # 68000 JSR/JMP absolute-long signatures (6-byte instructions).
        if opcode in (0x4EB9, 0x4EF9):
            target = be32(data, offset + 2)
            if 0x200 <= target < len(data) and not target & 1:
                counter = jsr_sources if opcode == 0x4EB9 else jump_sources
                counter[offset // BANK_BYTES] += 1
                if opcode == 0x4EB9:
                    jsr_targets[target] += 1
        # MULS.W and DIVS.W forms, excluding source addressing-mode bits.
        # These are *word-pattern matches*, not decoded instruction streams.
        if opcode & 0xF1C0 == 0xC1C0:
            muls_sources[offset // BANK_BYTES] += 1
        if opcode & 0xF1C0 == 0x81C0:
            divs_sources[offset // BANK_BYTES] += 1
    all_banks = set(jsr_sources) | set(jump_sources) | set(muls_sources) | set(divs_sources)
    windows = [{
        "offset_start": index * BANK_BYTES,
        "offset_end_exclusive": min((index + 1) * BANK_BYTES, len(data)),
        "jsr_absolute_long_refs": jsr_sources[index],
        "jmp_absolute_long_refs": jump_sources[index],
        "muls_word_patterns": muls_sources[index],
        "divs_word_patterns": divs_sources[index],
    } for index in sorted(all_banks)]
    # These are reference-count rankings, not guesses at actual game functions.
    entrypoint = be32(data, 4)
    return {
        "status": "static_68000_opcode_candidates_not_physics_disassembly",
        "sha256": hashlib.sha256(data).hexdigest(),
        "reset_pc": entrypoint,
        "jsr_absolute_long_total": sum(jsr_sources.values()),
        "distinct_jsr_targets": len(jsr_targets),
        "top_referenced_rom_targets": [
            {"offset": x, "reference_count": n}
            for x, n in jsr_targets.most_common(12)
        ],
        "regions": windows,
        "genesis_physics_routine_verified": False,
        "atari_arcade_source_code_present": False,
        "warning": "Raw patterns may also occur in data/operands; dynamic trace is required.",
    }


def inspect(data, require_identity=True):
    if require_identity:
        if len(data) != SOURCE_BYTES or hashlib.sha256(data).hexdigest() != SOURCE_HASH:
            raise ValueError("Race Drivin' Genesis ROM size or SHA-256 mismatch")
        if not data[0x100:0x120].startswith(b"SEGA GENESIS") or \
           data[0x180:0x18E].strip() != b"GM T-48246 -00":
            raise ValueError("Unexpected US Genesis header / product")
        if be32(data, 4) != 0x200:
            raise ValueError("Unexpected 68000 reset vector")
    return scan(data)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("rom", type=Path)
    p.add_argument("--output", type=Path, help="Optional small JSON summary; no ROM bytes")
    args = p.parse_args()
    report = json.dumps(inspect(args.rom.read_bytes()), sort_keys=True, indent=2) + "\n"
    if args.output:
        args.output.write_text(report)
    else:
        print(report, end="")


if __name__ == "__main__":
    main()
