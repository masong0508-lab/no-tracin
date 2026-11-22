"""Host coverage of rival ground alignment through the actual draw matrix.

Synthetic checks need no source ROM. VR_ROM optionally adds the original
Acropolis bank regression using generated headers in a temporary directory.
"""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class RivalPoseTests(unittest.TestCase):
    def run_host_check(self, optional, original_rom=False):
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Host C compiler not available (set HOST_CC)")
        with tempfile.TemporaryDirectory(prefix="notracin-rival-pose-") as directory:
            work = Path(directory)
            gba_header = (ROOT / "src/gba.h").read_text()
            arm_target = 'target("arm"), '
            self.assertEqual(gba_header.count(arm_target), 1)
            header = work / "gba_host.h"
            header.write_text(gba_header.replace(arm_target, ""))
            command = compiler + [
                "-std=c99", "-O2", "-ffunction-sections", "-fdata-sections",
                "-Wno-pointer-to-int-cast", "-include", str(header),
                "-I", str(ROOT / "src"), "-I", str(work),
                "-Wl,--gc-sections,--wrap=r_mesh,--wrap=r_box_mat,--wrap=r_visible,--wrap=r_depth",
                str(ROOT / "tests/rival_pose_host.c"),
                str(ROOT / "src/car.c"), str(ROOT / "src/render.c"),
            ]
            if optional:
                command.append("-DWITH_VR_ORIGINAL_TRACKS")
            if original_rom:
                imported = subprocess.run(
                    [sys.executable, str(ROOT / "tools/import_vr_original.py"),
                     "--rom", str(Path(os.environ["VR_ROM"]).expanduser()),
                     "--output-dir", str(work)],
                    capture_output=True, text=True, timeout=30,
                )
                self.assertEqual(imported.returncode, 0, imported.stdout + imported.stderr)
                command += ["-DTEST_ORIGINAL_ROM", str(ROOT / "src/track.c"),
                            str(ROOT / "src/vr_original.c")]
            binary = work / "rival_pose"
            command += ["-o", str(binary)]
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            executed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)
            self.assertIn("PASS:", executed.stdout)

    def test_custom_poses_in_stock_build(self):
        self.run_host_check(optional=False)

    def test_banking_and_custom_poses_in_optional_build(self):
        self.run_host_check(optional=True)

    @unittest.skipUnless(os.environ.get("VR_ROM"), "Set VR_ROM to test original Acropolis bank")
    def test_original_acropolis_bank(self):
        self.run_host_check(optional=True, original_rom=True)


if __name__ == "__main__":
    unittest.main()
