"""Arithmetic safety and force direction at the existing physics-lab limits."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class SoftbodyLimitsTests(unittest.TestCase):
    def run_host_checks(self,optional,friction=False):
        compiler=shlex.split(os.environ.get("HOST_CC","cc"))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest("Host C compiler not available (set HOST_CC)")
        with tempfile.TemporaryDirectory(prefix="notracin-softbody-arithmetic-") as directory:
            work=Path(directory)
            header=work/"gba_host.h"
            header.write_text((ROOT/"src/gba.h").read_text().replace('target("arm"), ',""))
            binary=work/"softbody_limits"
            command=compiler+[
                "-std=c99","-O2","-ffunction-sections","-fdata-sections",
                "-Wno-pointer-to-int-cast","-include",str(header),
                "-I",str(ROOT/"src"),"-Wl,--gc-sections",
                "-fsanitize=signed-integer-overflow,integer-divide-by-zero",
                str(ROOT/"tests/softbody_limits_host.c"),
                str(ROOT/"src/car.c"),str(ROOT/"src/render.c"),str(ROOT/"src/tune.c"),
                "-o",str(binary),
            ]
            if optional:command.append("-DWITH_VR_ORIGINAL_TRACKS")
            if friction:command.append("-DCHECK_FRICTION")
            r=subprocess.run(command,capture_output=True,text=True,timeout=30)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            env=dict(os.environ,UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
            # Spring extreme, no crumple, then low-gravity/high-grip impacts.
            cases=[("angular",),("stationary",),
                   (1,1),(2,1),(2,2),(2,3),
                   (3,0),(3,1),(3,2),(4,0),(4,1),(4,2)]
            if friction:cases=[("friction",)]
            for case in cases:
                with self.subTest(case=case):
                    r=subprocess.run([str(binary),*map(str,case)],env=env,
                                     capture_output=True,text=True,timeout=15)
                    self.assertEqual(r.returncode,0,r.stdout+r.stderr)
                    self.assertIn("PASS:",r.stdout)

    def test_stock_arithmetic(self):self.run_host_checks(False)
    def test_optional_arithmetic(self):self.run_host_checks(True)
    def test_exact_normal_and_wide_force_vectors(self):self.run_host_checks(False,True)

if __name__=="__main__":unittest.main()
