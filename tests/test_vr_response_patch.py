"""Native-ROM patch safety checks; no commercial fixtures are stored here.

Set VR_ROM to the supported original image to enable apply/restore checks.
"""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from patch_vr_response import (ORIGINAL_SHA256, PATCHED_SHA256, ROM_SIZE,
                               ips_bytes, transform)


def word_checksum(data):
    return sum((data[i] << 8) | data[i+1] for i in range(512,len(data),2)) & 65535


def read_ips(data, patch):
    """Independent reader for ordinary, non-RLE IPS records."""
    if patch[:5] != b"PATCH" or patch[-3:] != b"EOF":
        raise AssertionError("Invalid IPS boundaries")
    result = bytearray(data)
    position = 5
    while patch[position:position+3] != b"EOF":
        address = int.from_bytes(patch[position:position+3],"big")
        size = int.from_bytes(patch[position+3:position+5],"big")
        if not size or address+size > len(result):
            raise AssertionError("Unexpected IPS record")
        position += 5
        result[address:address+size] = patch[position:position+size]
        position += size
    if position != len(patch)-3:
        raise AssertionError("Trailing IPS data")
    return bytes(result)


class PatchInputTests(unittest.TestCase):
    def test_incomplete_and_headered_images_are_rejected(self):
        for size in (0,512,ROM_SIZE-1,ROM_SIZE+512):
            with self.subTest(size=size), self.assertRaisesRegex(ValueError,"Expected.*ROM bytes"):
                transform(bytes(size))

    def test_plausible_header_does_not_bypass_revision_hash(self):
        fake = bytearray(ROM_SIZE)
        fake[0x100:0x110] = b"SEGA MEGA DRIVE "
        fake[0x295CE:0x295D2] = bytes.fromhex("83fc00e1")
        fake[0x18E:0x190] = word_checksum(fake).to_bytes(2,"big")
        with self.assertRaisesRegex(ValueError,"Unsupported input SHA256"):
            transform(bytes(fake))

    def test_output_paths_are_checked_before_any_write(self):
        with tempfile.TemporaryDirectory(prefix="vr-patch-paths-") as directory:
            work = Path(directory)
            source, existing, new = work/"source.md", work/"existing.md", work/"new.md"
            source.write_bytes(b"fictional source fixture")
            existing.write_bytes(b"preserve this output")
            alias = work/"alias.md"
            alias.symlink_to(source)
            cases = (
                ([str(source),str(source)],"different files"),
                ([str(source),str(alias)],"different files"),
                ([str(source),str(existing)],"Refusing to overwrite"),
                ([str(source),str(new),"--ips",str(source)],"own path"),
                ([str(source),str(new),"--ips",str(new)],"own path"),
            )
            for arguments,message in cases:
                with self.subTest(arguments=arguments):
                    result = subprocess.run([sys.executable,str(ROOT/"tools/patch_vr_response.py")]+arguments,
                                            capture_output=True,text=True,timeout=10)
                    self.assertEqual(result.returncode,2,result.stdout+result.stderr)
                    self.assertIn(message,result.stderr)
                    self.assertFalse(new.exists())
                    self.assertEqual(source.read_bytes(),b"fictional source fixture")
                    self.assertEqual(existing.read_bytes(),b"preserve this output")


@unittest.skipUnless(os.environ.get("VR_ROM"),"Set VR_ROM for tests using your original ROM")
class OriginalResponsePatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = Path(os.environ["VR_ROM"]).expanduser()
        cls.data = cls.source.read_bytes()
        cls.patched = transform(cls.data)

    def test_exact_changed_offsets_hash_and_checksum(self):
        self.assertEqual(hashlib.sha256(self.data).hexdigest(),ORIGINAL_SHA256)
        self.assertEqual(hashlib.sha256(self.patched).hexdigest(),PATCHED_SHA256)
        self.assertEqual(len(self.patched),ROM_SIZE)
        self.assertEqual([i for i,(a,b) in enumerate(zip(self.data,self.patched)) if a != b],
                         [0x18F,0x295D1])
        self.assertEqual(self.patched[0x295CE:0x295D2],bytes.fromhex("83fc00b4"))
        self.assertEqual(word_checksum(self.data),0x95AB)
        self.assertEqual(word_checksum(self.patched),0x957E)
        self.assertEqual(int.from_bytes(self.patched[0x18E:0x190],"big"),word_checksum(self.patched))

    def test_restore_reconstructs_every_original_byte(self):
        self.assertEqual(transform(self.patched,restore=True),self.data)
        self.assertEqual(self.source.read_bytes(),self.data)

    def test_both_ips_directions_match_the_strict_patcher(self):
        self.assertEqual(len(ips_bytes()),22)
        self.assertEqual(len(ips_bytes(restore=True)),22)
        self.assertEqual(read_ips(self.data,ips_bytes()),self.patched)
        self.assertEqual(read_ips(self.patched,ips_bytes(restore=True)),self.data)

    def test_reapplication_and_modified_revisions_are_rejected(self):
        modified = bytearray(self.data)
        modified[0x400] ^= 1
        modified[0x18E:0x190] = word_checksum(modified).to_bytes(2,"big")
        for data,restore in ((self.patched,False),(self.data,True),(bytes(modified),False)):
            with self.subTest(restore=restore), self.assertRaisesRegex(ValueError,"Unsupported input SHA256"):
                transform(data,restore)

    def test_cli_preserves_source_and_restores_to_new_files(self):
        with tempfile.TemporaryDirectory(prefix="vr-patch-roundtrip-") as directory:
            work = Path(directory)
            patched,restored,ips = work/"patched.md",work/"restored.md",work/"change.ips"
            tool = [sys.executable,str(ROOT/"tools/patch_vr_response.py")]
            applied = subprocess.run(tool+[str(self.source),str(patched),"--ips",str(ips)],
                                     capture_output=True,text=True,timeout=10)
            self.assertEqual(applied.returncode,0,applied.stdout+applied.stderr)
            reverted = subprocess.run(tool+[str(patched),str(restored),"--restore"],
                                      capture_output=True,text=True,timeout=10)
            self.assertEqual(reverted.returncode,0,reverted.stdout+reverted.stderr)
            self.assertEqual(patched.read_bytes(),self.patched)
            self.assertEqual(restored.read_bytes(),self.data)
            self.assertEqual(ips.read_bytes(),ips_bytes())
            self.assertEqual(self.source.read_bytes(),self.data)


if __name__ == "__main__":
    unittest.main()
