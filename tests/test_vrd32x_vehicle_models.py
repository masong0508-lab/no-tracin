"""Source-only fixtures plus an opt-in private exact-ROM validation."""
import hashlib
import os
from pathlib import Path
import struct
import unittest
from tools import export_vrd32x_vehicle_models as v


def packed(fmt, *values):
    return struct.pack('>'+fmt,*values)


def fixture():
    # Triangle followed by strip quad, with a reused normal. Coordinates are
    # signed 16-bit values, while the model pointer is an SH-2 SDRAM address.
    return (packed('HH12h',4,0,-32,0,32,32,0,32,32,0,-32,-32,0,-32)
            +packed('9H',0x21,0x4601,0,16,32,0,0x4000,0,0x0200)
            +packed('4H',0x4700,48,0,0x0C00))


class VehicleModels(unittest.TestCase):
    def test_banked_signed_geometry_and_inherited_normal(self):
        model=fixture(); data=bytes(16)+model+bytes(16)
        mesh=v.decode_model(data,(16,16+len(model)),v.BANK_DEST)
        self.assertEqual(mesh['rom_offset'],16)
        self.assertEqual(mesh['end_offset'],16+len(model))
        self.assertEqual(mesh['vertices'][0],(-32,0,32))
        self.assertEqual([f['vertices'] for f in mesh['faces']],[[0,1,2],[0,2,3,0]])
        self.assertEqual([f['normal_updated'] for f in mesh['faces']],[True,False])
        self.assertEqual(mesh['faces'][1]['normal'],(0,0x4000,0))
        self.assertEqual([f['palette_index'] for f in mesh['faces']],[0x46,0x47])

    def test_bank_bounds_and_corrupt_index(self):
        model=fixture(); data=bytes(16)+model
        for ptr in (v.BANK_DEST-2,v.BANK_DEST+1,v.BANK_DEST+len(model)):
            with self.assertRaises(ValueError):v.decode_model(data,(16,len(data)),ptr)
        with self.assertRaises(ValueError):v.decode_model(data,(16,len(data)-2),v.BANK_DEST)
        damaged=bytearray(model);struct.pack_into('>H',damaged,32,0x40)
        with self.assertRaisesRegex(ValueError,'vertex index'):
            v.decode_model(damaged,(0,len(damaged)),v.BANK_DEST)

    def test_bank_range_and_program_pointer_validation(self):
        rom=bytearray(v.BANK_TABLE+16)
        struct.pack_into('>II',rom,v.BANK_TABLE,0x22000010,0x22000080)
        self.assertEqual(v.bank_range(rom,0),(16,128))
        struct.pack_into('>I',rom,v.BANK_TABLE,0x0603C000)
        with self.assertRaises(ValueError):v.bank_range(rom,0)
        struct.pack_into('>I',rom,v.PLAYER_TABLE,0x930534)
        with self.assertRaises(ValueError):v.code_row(rom,v.PLAYER_TABLE,0,17)

    def assert_lossless_batches(self, mesh):
        expected=[(tuple(mesh['vertices'][i] for i in f['vertices']),
                   {k:x for k,x in f.items() if k!='vertices'}) for f in mesh['faces']]
        actual=[]
        for batch in v.batches(mesh):
            self.assertLessEqual(len(batch['vertices']),48)
            self.assertLessEqual(len(batch['faces']),255)
            actual.extend((tuple(batch['vertices'][i] for i in f['vertices']),
                           {k:x for k,x in f.items() if k!='vertices'}) for f in batch['faces'])
        self.assertEqual(actual,expected)

    def test_lossless_batch_split(self):
        mesh=dict(vertices=[(i,0,0) for i in range(90)],faces=[dict(vertices=[i,i+1,i+2],header=1,colour=i) for i in range(0,90,3)])
        self.assertEqual(len(v.batches(mesh)),2)
        self.assert_lossless_batches(mesh)
        # Many faces with shared coordinates must also honor the u8 face cap.
        mesh['faces']=[dict(vertices=[0,1,2],header=1,colour=0)]*300
        self.assertEqual([len(b['faces']) for b in v.batches(mesh)],[255,45])
        self.assert_lossless_batches(mesh)

    @unittest.skipUnless(os.environ.get('VRD32X_ROM'),'owner ROM not supplied')
    def test_exact_owner_models(self):
        rom=Path(os.environ['VRD32X_ROM']).read_bytes()
        self.assertEqual(hashlib.sha256(rom).hexdigest(),v.SHA256)
        pack=v.extract(rom)
        self.assertEqual([len(c['meshes']) for c in pack['cars']],[33,31,31])
        self.assertEqual([len(c['meshes'][c['player_slots']['0']]['faces']) for c in pack['cars']],[89,110,103])
        for c in pack['cars']:
            self.assertEqual(len(c['opponent_lods']),4)
            self.assertEqual(len(c['opponent_axles']),2)
            self.assertEqual(len(c['player_attachment_xyz']),3)
            for mesh in c['meshes'].values():
                self.assert_lossless_batches(mesh)
                self.assertLess(mesh['rom_offset'],mesh['end_offset'])
            for lod in c['opponent_lods']:
                self.assertEqual(set(lod),set(v.LODS))
                self.assertGreater(len(c['meshes'][lod['near']]['faces']),len(c['meshes'][lod['far']]['faces']))


if __name__=='__main__':unittest.main()
