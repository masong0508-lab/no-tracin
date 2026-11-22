"""Check cartridge fingerprints without checking proprietary ROMs into Git."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("cart", ROOT/"tools/audit_uploaded_cartridges.py")
cart = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cart)

def sample(system):
    b = bytearray(0x800)
    b[4:8] = (0x200).to_bytes(4,"big")
    b[0x100:0x100+len(system)] = system
    b[0x150:0x158] = b"TEST ROM"
    b[0x180:0x18e] = b"GM TEST000-00 "
    c = sum((b[i]<<8)|b[i+1] for i in range(0x200,len(b),2))&0xffff
    b[0x18e:0x190] = c.to_bytes(2,"big")
    return bytes(b)

class Cartridges(unittest.TestCase):
    def test_read_only_two_families(self):
        with tempfile.TemporaryDirectory() as t:
            for system in (b"SEGA GENESIS", b"SEGA 32X U"):
                path = Path(t)/"rom.bin"
                raw = sample(system); path.write_bytes(raw)
                time = path.stat().st_mtime_ns
                info = cart.inspect(path)
                self.assertFalse(info["known_revision"])
                self.assertFalse(info["deluxe_courses_imported"])
                self.assertEqual(path.read_bytes(),raw)
                self.assertEqual(path.stat().st_mtime_ns,time)
                with self.assertRaisesRegex(ValueError,"Unknown full-file"):
                    cart.inspect(path,True)

    def test_reject_corrupt_and_misidentified(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/"rom.bin"
            b=bytearray(sample(b"SEGA 32X U"));b[0x200]^=1;p.write_bytes(b)
            with self.assertRaisesRegex(ValueError,"checksum"):cart.inspect(p)
            b=bytearray(sample(b"SEGA GENESIS"));b[4:8]=b"\x00\x00\x00\x01";p.write_bytes(b)
            with self.assertRaisesRegex(ValueError,"reset"):cart.inspect(p)

    def test_32x_never_enters_genesis_svp_importer(self):
        import subprocess
        import sys
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            fake = base / "fake32x.rom"
            fake.write_bytes(sample(b"SEGA 32X U"))
            cases = [fake]
            supplied = os.environ.get("VR32X_ROM")
            if supplied and Path(supplied).is_file():
                cases.append(Path(supplied))
            for i, source in enumerate(cases):
                output = base / ("wrong-import-" + str(i))
                result = subprocess.run([
                    sys.executable, str(ROOT / "tools/import_vr_original.py"),
                    "--rom", str(source), "--output-dir", str(output)],
                    text=True, capture_output=True, timeout=20)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())

    def test_exact_uploaded_roms_when_supplied(self):
        sources = ((os.environ.get("RACE_DRIVIN_ROM"),"race_drivin_genesis"),
                   (os.environ.get("VR32X_ROM"),"virtua_racing_deluxe_32x"))
        if not all(s and Path(s).is_file() for s,_ in sources):
            self.skipTest("Set RACE_DRIVIN_ROM and VR32X_ROM for private ROM checks")
        for path,family in sources:
            with self.subTest(family=family):
                info=cart.inspect(path,True)
                self.assertEqual(info["family"],family)
                self.assertTrue(info["known_revision"])

if __name__ == "__main__":
    unittest.main()
