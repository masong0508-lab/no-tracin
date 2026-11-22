"""Host regressions on production car physics across all four menu profiles."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class DrivingProfileTests(unittest.TestCase):
    def check(self, optional):
        cc = shlex.split(os.environ.get("HOST_CC", "cc"))
        if not cc or not shutil.which(cc[0]):
            self.skipTest("Host compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="notracin-profile-") as path:
            d = Path(path)
            header = d / "gba_host.h"
            header.write_text((ROOT / "src/gba.h").read_text().replace('target("arm"), ', ""))
            cmd = cc + ["-std=c99", "-O2", "-ffunction-sections", "-fdata-sections",
                        "-Wno-pointer-to-int-cast", "-include", str(header),
                        "-fsanitize=shift,signed-integer-overflow,integer-divide-by-zero",
                        "-fno-sanitize-recover=all", "-I", str(ROOT / "src"),
                        "-Wl,--gc-sections", str(ROOT / "tests/driving_profiles_host.c"),
                        str(ROOT / "src/car.c"), str(ROOT / "src/tune.c"),
                        str(ROOT / "src/render.c"), "-o", str(d / "profiles")]
            if optional: cmd += ["-DWITH_VR_ORIGINAL_TRACKS"]
            built = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            executed = subprocess.run([str(d / "profiles")], capture_output=True, text=True, timeout=20,
                    env={**os.environ, "UBSAN_OPTIONS":"halt_on_error=1"})
            self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)
            self.assertIn("PASS:", executed.stdout)
    def test_standard_profiles(self): self.check(False)
    def test_optional_profiles(self): self.check(True)

if __name__ == "__main__": unittest.main()
