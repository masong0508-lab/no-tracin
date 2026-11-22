"""ROM-free 32X header checks; no proprietary game data needed."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import probe_vr_32x


class Probe32xTests(unittest.TestCase):
    @staticmethod
    def fake_cart(header=b"SEGA 32X        ", title=b"VIRTUA RACING DELUXE"):
        rom = bytearray(0x800)
        rom[0x100:0x100 + len(header)] = header
        rom[0x150:0x150 + len(title)] = title
        rom[0x180:0x18E] = b"GM 12345678 -00"
        rom[0x1F0:0x1F4] = b"JUE "
        return bytes(rom)

    def test_32x_header_and_hash_are_read_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "owned.32x"
            contents = self.fake_cart()
            path.write_bytes(contents)
            before = path.stat().st_mtime_ns
            result = probe_vr_32x.inspect(path, hashlib.sha256(contents).hexdigest())
            self.assertEqual(result["status"], "32x_header_identified_only")
            self.assertFalse(result["course_import_supported"])
            self.assertIn("VIRTUA RACING", result["international_title"])
            self.assertEqual(path.read_bytes(), contents)
            self.assertEqual(path.stat().st_mtime_ns, before)

    def test_genesis_svp_image_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "genesis.bin"
            path.write_bytes(self.fake_cart(header=b"SEGA MEGA DRIVE "))
            with self.assertRaisesRegex(ValueError, "32X"):
                probe_vr_32x.inspect(path)

    def test_wrong_hash_and_short_cart_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "owned.32x"
            path.write_bytes(self.fake_cart())
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                probe_vr_32x.inspect(path, "0" * 64)
            path.write_bytes(b"SEGA 32X")
            with self.assertRaisesRegex(ValueError, "Too small"):
                probe_vr_32x.inspect(path)

    def test_cli_does_not_create_import_data(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "owned.32x"
            path.write_bytes(self.fake_cart())
            p = subprocess.run([sys.executable, str(ROOT / "tools/probe_vr_32x.py"), str(path)],
                               capture_output=True, text=True)
            self.assertEqual(p.returncode, 0, p.stderr)
            self.assertIn('"course_import_supported": false', p.stdout)
            self.assertEqual([p.name for p in Path(tmp).iterdir()], ["owned.32x"])

if __name__ == "__main__":
    unittest.main()
