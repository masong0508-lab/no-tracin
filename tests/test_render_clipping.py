"""Sanitize actual renderer clipping and fill routines, including negative X."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RendererClippingTests(unittest.TestCase):
    def test_signed_near_plane_and_negative_screen_edges(self):
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Host C compiler not available")
        with tempfile.TemporaryDirectory(prefix="notracin-render-clip-") as directory:
            work = Path(directory)
            header = work / "gba_host.h"
            header.write_text((ROOT / "src/gba.h").read_text().replace('target("arm"), ', ""))
            binary = work / "clip_test"
            cmd = compiler + [
                "-O2", "-std=c99", "-Wno-pointer-to-int-cast", "-fsanitize=shift",
                "-fno-sanitize-recover=shift", "-include", str(header),
                "-I", str(ROOT / "src"), str(ROOT / "tests/render_clipping_host.c"),
                "-o", str(binary),
            ]
            result = subprocess.run(cmd, text=True, capture_output=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], text=True, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PASS:", result.stdout)


if __name__ == "__main__":
    unittest.main()
