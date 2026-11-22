"""Source-only and donor-backed checks of height-free route lookup."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TrackProjectionTests(unittest.TestCase):
    def run_projection(self, donor):
        cc = shlex.split(os.environ.get("HOST_CC", "cc"))
        if not cc or not shutil.which(cc[0]):
            self.skipTest("Host compiler is unavailable")
        rom = os.environ.get("VR_ROM")
        if donor and not rom:
            self.skipTest("Set VR_ROM to test height skipping on original courses")
        with tempfile.TemporaryDirectory(prefix="notracin-track-projection-") as temp:
            temp = Path(temp)
            host_header = temp / "gba_host.h"
            gba = (ROOT / "src/gba.h").read_text()
            self.assertEqual(gba.count('target("arm"), '), 1)
            host_header.write_text(gba.replace('target("arm"), ', ""))
            args = cc + ["-std=c99", "-O2", "-ffunction-sections", "-fdata-sections",
                         "-Wno-pointer-to-int-cast", "-fsanitize=shift",
                         "-fno-sanitize-recover=shift", "-include", str(host_header),
                         "-I", str(ROOT / "src"), "-Wl,--gc-sections"]
            if donor:
                build = temp / "imported"
                imported = subprocess.run(["python3", str(ROOT / "tools/import_vr_original.py"),
                                           "--rom", rom, "--output-dir", str(build)],
                                          capture_output=True, text=True, timeout=60)
                self.assertEqual(imported.returncode, 0, imported.stdout + imported.stderr)
                args += ["-DWITH_VR_ORIGINAL_TRACKS", "-I", str(build),
                         "-Wl,--wrap=vr_original_height"]
            args += [str(ROOT / "tests/track_projection_host.c"),
                     str(ROOT / "src/track.c"), str(ROOT / "src/render.c")]
            if donor:
                args.append(str(ROOT / "src/vr_original.c"))
            args += ["-o", str(temp / "projection")]
            built = subprocess.run(args, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(temp / "projection")], capture_output=True,
                                 text=True, timeout=20,
                                 env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("PASS:", run.stdout)

    def test_custom_course_fast_projection(self):
        self.run_projection(donor=False)

    def test_original_course_fast_projection(self):
        self.run_projection(donor=True)


if __name__ == "__main__":
    unittest.main()
