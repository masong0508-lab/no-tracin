"""Clock/checkpoint/lap regressions using the actual race state machine."""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class RaceTransitionTests(unittest.TestCase):
    def run_host_check(self, optional):
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Host C compiler not available (set HOST_CC)")
        with tempfile.TemporaryDirectory(prefix="notracin-race-transitions-") as directory:
            work = Path(directory)
            gba_header = (ROOT / "src/gba.h").read_text()
            arm_target = 'target("arm"), '
            self.assertEqual(gba_header.count(arm_target), 1)
            header = work / "gba_host.h"
            header.write_text(gba_header.replace(arm_target, ""))
            binary = work / "race_transitions"
            command = compiler + [
                "-std=c99", "-O2", "-ffunction-sections", "-fdata-sections",
                "-Wno-pointer-to-int-cast", "-include", str(header),
                "-I", str(ROOT / "src"), "-Wl,--gc-sections",
                str(ROOT / "tests/race_transitions_host.c"), str(ROOT / "src/render.c"),
                "-o", str(binary),
            ]
            if optional:
                command.append("-DWITH_VR_ORIGINAL_TRACKS")
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            executed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)
            self.assertIn("PASS:", executed.stdout)

    def test_stock_transitions(self):
        self.run_host_check(optional=False)

    def test_optional_course_transitions(self):
        self.run_host_check(optional=True)


if __name__ == "__main__":
    unittest.main()
