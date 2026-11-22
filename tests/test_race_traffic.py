"""Lapped traffic regressions executing production race and collision code."""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class RaceTrafficTests(unittest.TestCase):
    def run_host_checks(self, optional):
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Host C compiler not available (set HOST_CC)")
        with tempfile.TemporaryDirectory(prefix="notracin-race-traffic-") as directory:
            work = Path(directory)
            gba_header = (ROOT / "src/gba.h").read_text()
            arm_target = 'target("arm"), '
            self.assertEqual(gba_header.count(arm_target), 1)
            header = work / "gba_host.h"
            header.write_text(gba_header.replace(arm_target, ""))
            binary = work / "race_traffic"
            command = compiler + [
                "-std=c99", "-O2", "-ffunction-sections", "-fdata-sections",
                "-Wno-pointer-to-int-cast", "-include", str(header),
                "-I", str(ROOT / "src"), "-Wl,--gc-sections",
                str(ROOT / "tests/race_traffic_host.c"), str(ROOT / "src/render.c"),
                str(ROOT / "src/softbody.c"), str(ROOT / "src/tune.c"),
                "-o", str(binary),
            ]
            if optional:
                command.append("-DWITH_VR_ORIGINAL_TRACKS")
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            for case in ("contact-softbody", "contact-arcade", "following-player",
                         "following-rival", "slipstream", "softbody-draft", "far", "standings",
                         "rejoin-draft", "rejoin-following", "rejoin-culling",
                         "projection-missing", "rejoin-standings"):
                with self.subTest(case=case):
                    executed = subprocess.run([str(binary), case], capture_output=True,
                                              text=True, timeout=10)
                    self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)
                    self.assertIn("PASS:", executed.stdout)

    def test_stock_traffic(self):
        self.run_host_checks(optional=False)

    def test_optional_traffic(self):
        self.run_host_checks(optional=True)


if __name__ == "__main__":
    unittest.main()
