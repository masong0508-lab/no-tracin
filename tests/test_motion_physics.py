"""Check real gameplay C Newton motion separately from steering settings."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
class MotionPhysicsTests(unittest.TestCase):
    def check(self,optional):
        compiler=shlex.split(os.environ.get("HOST_CC","cc"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="notracin-motion-") as directory:
            work=Path(directory)
            header=work/"gba_host.h"
            header.write_text((ROOT/"src/gba.h").read_text().replace('target("arm"), ',""))
            cmd=compiler+["-std=c99","-O2","-ffunction-sections","-fdata-sections",
                "-Wno-pointer-to-int-cast","-include",str(header),
                "-fsanitize=shift,signed-integer-overflow,integer-divide-by-zero",
                "-fno-sanitize-recover=all","-I",str(ROOT/"src"),
                "-Wl,--gc-sections",str(ROOT/"tests/motion_physics_host.c"),
                str(ROOT/"src/car.c"),str(ROOT/"src/render.c"),
                str(ROOT/"src/tune.c"),"-o",str(work/"run")]
            if optional: cmd.append("-DWITH_VR_ORIGINAL_TRACKS")
            built=subprocess.run(cmd,capture_output=True,text=True,timeout=35)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(work/"run")],capture_output=True,text=True,
                    timeout=20,env={**os.environ,"UBSAN_OPTIONS":"halt_on_error=1"})
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn("PASS:",run.stdout)
    def test_custom(self):self.check(False)
    def test_optional(self):self.check(True)

if __name__=="__main__":unittest.main()
