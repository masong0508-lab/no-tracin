"""Persistent external velocity additions on the production soft body."""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SoftbodyVelocityTests(unittest.TestCase):
    def run_host_check(self, optional):
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Host C compiler not available (set HOST_CC)")
        with tempfile.TemporaryDirectory(prefix="notracin-softbody-velocity-") as directory:
            work = Path(directory)
            gba_header = (ROOT / "src/gba.h").read_text()
            arm_target = 'target("arm"), '
            self.assertEqual(gba_header.count(arm_target), 1)
            header = work / "gba_host.h"
            header.write_text(gba_header.replace(arm_target, ""))
            binary = work / "softbody_velocity"
            command = compiler + [
                "-std=c99", "-O2", "-ffunction-sections", "-fdata-sections",
                "-Wno-pointer-to-int-cast", "-include", str(header),
                "-I", str(ROOT / "src"), "-Wl,--gc-sections",
                str(ROOT / "tests/softbody_velocity_host.c"), str(ROOT / "src/render.c"),
                str(ROOT / "src/tune.c"), "-o", str(binary),
            ]
            if optional:
                command.append("-DWITH_VR_ORIGINAL_TRACKS")
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            executed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)
            self.assertIn("PASS:", executed.stdout)

    def test_stock_velocity(self):
        self.run_host_check(optional=False)

    def test_optional_velocity(self):
        self.run_host_check(optional=True)


if __name__ == "__main__":
    unittest.main()
