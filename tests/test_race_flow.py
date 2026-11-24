"""Race direction, finish chronology and recovery credit on production C."""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CASES = (
    "wrong-way-motion", "wrong-way-cone", "wrong-way-projection-loss", "finish-prior", "finish-same",
    "finish-tie", "finish-subunit", "respawn-back", "respawn-cp", "respawn-lap",
    "respawn-forward", "respawn-grid", "jump-guard",
)


class RaceFlowTests(unittest.TestCase):
    def run_host_checks(self, optional, real_finish=False, study=False):
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Host C compiler not available (set HOST_CC)")
        with tempfile.TemporaryDirectory(prefix="notracin-race-flow-") as directory:
            work = Path(directory)
            gba = (ROOT / "src/gba.h").read_text()
            self.assertEqual(gba.count('target("arm"), '), 1)
            header = work / "gba_host.h"
            header.write_text(gba.replace('target("arm"), ', ""))
            binary = work / "race_flow"
            command = compiler + [
                "-std=c99", "-O2", "-ffunction-sections", "-fdata-sections",
                "-Wno-pointer-to-int-cast", "-fsanitize=shift", "-fno-sanitize-recover=shift", "-include", str(header),
                "-I", str(ROOT / "src"), "-Wl,--gc-sections",
                str(ROOT / "tests" / ("race_finish_host.c" if real_finish else "race_flow_host.c")),
                *[str(ROOT / "src" / name) for name in
                  ("car.c", "softbody.c", "tune.c", "render.c")],
                "-o", str(binary),
            ]
            if optional:
                command.append("-DWITH_VR_ORIGINAL_TRACKS")
            if study:
                command.append("-DWITH_VRD_32X_TRACKS")
            if real_finish:
                command.append(str(ROOT / "src/track.c"))
            built = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            cases = ("study-free-drive",) if study else (("player-first", "rival-first", "tie", "prior-rival") if real_finish else CASES + (("original-branch-recovery",) if optional else ()))
            for case in cases:
                with self.subTest(case=case):
                    run = subprocess.run([str(binary), case], capture_output=True,
                                         text=True, timeout=15)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                    self.assertIn("PASS:", run.stdout)

    def test_stock_race_flow(self):
        self.run_host_checks(False)

    def test_optional_race_flow(self):
        self.run_host_checks(True)

    def test_32x_study_real_road_respawn_without_fake_laps(self):
        self.run_host_checks(True, study=True)

    def test_fractional_finishes_across_actual_stock_course_seam(self):
        self.run_host_checks(False, real_finish=True)


if __name__ == "__main__":
    unittest.main()
