"""Host checks for stock/optional race saves; no ROM, emulator or ARM SDK.

Run with: python3 -m unittest discover -s tests -p 'test_*.py'
HOST_CC may name a host C compiler. Only the SRAM address in a temporary copy
of menu.c is redirected; production headers and sources remain unchanged.
"""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CourseRecordsTests(unittest.TestCase):
    def run_host_check(self, optional):
        compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
        self.assertTrue(compiler and shutil.which(compiler[0]), "A host C compiler is required (set HOST_CC)")
        menu_source = (ROOT / "src/menu.c").read_text()
        hardware_sram = "#define SRAM ((volatile u8 *)0x0E000000)"
        self.assertEqual(menu_source.count(hardware_sram), 1, "Update the host SRAM fixture for the new source")
        host_source = menu_source.replace(
            hardware_sram,
            "extern volatile u8 test_sram[0x8000];\n#define SRAM test_sram",
        )
        with tempfile.TemporaryDirectory(prefix="notracin-records-") as directory:
            work = Path(directory)
            menu_host = work / "menu_host.c"
            menu_host.write_text(host_source)
            binary = work / "course_records"
            command = compiler + [
                "-std=c99", "-O2", "-ffunction-sections", "-fdata-sections",
                "-Wno-pointer-to-int-cast", "-D__attribute__(x)=",
                "-I", str(ROOT / "src"), "-Wl,--gc-sections",
            ]
            if optional:
                command.append("-DWITH_VR_ORIGINAL_TRACKS")
            command += [str(ROOT / "tests/course_records_host.c"), str(menu_host), "-o", str(binary)]
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            executed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)
            self.assertIn("PASS:", executed.stdout)

    def test_stock_save_compatibility(self):
        self.run_host_check(optional=False)

    def test_optional_save_compatibility(self):
        self.run_host_check(optional=True)


if __name__ == "__main__":
    unittest.main()
