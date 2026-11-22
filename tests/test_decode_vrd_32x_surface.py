"""Actual 32X road BSP/height plane: synthetic 68000 branch witnesses and ROM parity."""
import os
from pathlib import Path
import unittest
from tools.decode_vrd_32x_surface import (
    RoadSurface, adjacent_edge_profile, asl16_with_carry, plane_eval,
    signed16, signed32, test_bsp_group, summarize_surface,
)
from tools.inspect_vrd_32x_tables import NORMAL_TABLE, ROM_SHA256


def group(flags=None, *, height=(6, -4, 1000), attrib=0x100, tail=0xABCD):
    """Assemble one 28-byte group with four signed 68000 half-space tests.

    Each flag is (kind, skip-bit, operands), where kind is one of the four
    disassembled branches, not an inferred vertex format.
    """
    if flags is None:
        flags = [
            ('x_direct',1,(100,0)),         # skip if X <100
            ('z_direct',1,(800,0)),         # skip if Z>=800
            ('z_by_x',0,(0,300)),          # skip if Z<300
            ('x_by_z',1,(0,300)),          # skip if X<300
        ]
    desc={'x_direct':(0,1),'z_direct':(1,1),
          'z_by_x':(0,0),'x_by_z':(1,0)}
    bits=[];words=[]
    for kind,skip,args in flags:
        a,b=desc[kind]
        bits.extend([a,b,skip]);words.extend(args)
    if len(words)!=8:raise ValueError('Four half-space tests are required')
    selector=int(''.join(str(x) for x in bits)+'0000',2)
    unsigned=lambda x:x & 0xffff
    packed=[selector]+list(map(unsigned,words+list(height)+[attrib,tail]))
    assert len(packed)==14
    return b''.join(x.to_bytes(2,'big') for x in packed)


def fixture(groups=(group(),), active_cell=(32,32)):
    """A synthetic legal 64x64 map and counted BSP record, no copyrighted bytes."""
    raw=bytearray(0x140000)
    raw[NORMAL_TABLE:NORMAL_TABLE+4]=(0x901000).to_bytes(4,'big')
    raw[NORMAL_TABLE+4:NORMAL_TABLE+8]=(0x910000).to_bytes(4,'big')
    for i in range(4096):
        raw[0x101000+2*i:0x101002+2*i]=(0xffff).to_bytes(2,'big')
    idx=active_cell[0]*64+active_cell[1]
    raw[0x101000+2*idx:0x101002+2*idx]=b'\x00\x00'
    base=0x110000
    raw[base:base+2]=(len(groups)-1).to_bytes(2,'big')
    pos=base+2
    for g in groups:
        raw[pos:pos+28]=g;pos+=28
    raw[pos:pos+2]=b'\xff\xff'
    return bytes(raw)


class RoadBSPTests(unittest.TestCase):
    def test_signed_68000_math(self):
        self.assertEqual(signed16(0xffff),-1)
        self.assertEqual(signed16(0x7fff),32767)
        self.assertEqual(signed32(0xffffffff),-1)
        self.assertEqual(asl16_with_carry(0x8000),(0,True))
        self.assertEqual(asl16_with_carry(0x4001),(0x8002,False))

    def test_four_independent_half_space_paths(self):
        sample=group()
        self.assertTrue(test_bsp_group(sample,0,512,512))
        self.assertFalse(test_bsp_group(sample,0, 60,512))   # x direct below 100
        self.assertFalse(test_bsp_group(sample,0,512,900))   # z direct above 800
        self.assertFalse(test_bsp_group(sample,0,512,200))   # z_by_x below 300
        self.assertFalse(test_bsp_group(sample,0,200,512))   # x_by_z below 300
        self.assertEqual(plane_eval(sample,0,512,512).height,516)

    def test_signed_height_plane_and_low_byte_mode(self):
        low=group(attrib=0x100, height=(6,-4,1000))
        high=group(attrib=0x180, height=(6,-4,1000))
        self.assertEqual(plane_eval(low,0,512,512).height,516)
        self.assertEqual(plane_eval(high,0,512,512).height,1032)
        self.assertEqual(plane_eval(low,0,512,1022).height,484)
        self.assertRaisesRegex(ValueError,'unnormalized',plane_eval,low,0,-1,0)
        self.assertRaisesRegex(ValueError,'outside ROM',test_bsp_group,low,4,1,1)

    def test_full_counted_record_and_world_coordinate_lookup(self):
        r=RoadSurface(fixture(),0,verify_identity=False)
        yes=r.sample_world(256,256)
        self.assertIsNotNone(yes)
        self.assertEqual(yes.height,516)
        self.assertEqual((yes.coeff_x,yes.coeff_z,yes.coeff_c,yes.attrib,yes.tail_word),
                         (6,-4,1000,0x100,0xabcd))
        self.assertIsNone(r.sample_world(20,256))
        self.assertIsNone(r.sample_world(256,256+512))
        self.assertRaisesRegex(ValueError,'outside',r.sample_world,60000,0)
        self.assertRaisesRegex(ValueError,'unverified',RoadSurface,fixture(),5,False,verify_identity=False)
        first=group(flags=[('x_direct',1,(2048,0))]*4)
        after=group(height=(0,0,2048))
        second=RoadSurface(fixture((first,after)),0,verify_identity=False)
        hit=second.sample_world(256,256)
        self.assertEqual(hit.group,1)
        self.assertEqual(hit.height,1024)

    def test_damaged_record_is_rejected(self):
        raw=bytearray(fixture());raw[0x110000:0x110002]=b'\x00\xff'
        r=RoadSurface(bytes(raw),0,verify_identity=False)
        with self.assertRaisesRegex(ValueError,'bad counted'):r.sample_world(256,256)
        with self.assertRaisesRegex(ValueError,'alternate slot 3'):RoadSurface(fixture(),0,True,verify_identity=False)

    def test_signed_negative_world_coordinates(self):
        raw=fixture(active_cell=(31,31))
        r=RoadSurface(raw,0,verify_identity=False)
        self.assertIsNotNone(r.sample_world(-256,-256))
        self.assertIsNone(r.sample_world(256,256))

    def test_exact_owned_rom_boundaries_and_height_planes(self):
        source=os.environ.get('VR32X_ROM')
        if not source:self.skipTest('Set VR32X_ROM for actual private 32X surface check')
        path=Path(source);before=path.stat().st_mtime_ns
        raw=path.read_bytes();result=summarize_surface(raw)
        self.assertEqual(result['sha256'],ROM_SHA256)
        profile=result['profiles']
        self.assertEqual([x['shared_inside_edges'] for x in profile],
                         [1689,1832,1563,1262,3153])
        self.assertEqual([x['road_classification_disagreements'] for x in profile],
                         [5,8,4,2,7])
        self.assertEqual([x['height_difference_p99'] for x in profile],
                         [19,17,17,17,21])
        self.assertEqual([x['height_difference_max'] for x in profile],
                         [49,21,35,18,39])
        samples=[(0,9,26,2351),(1,8,19,5760),(2,11,35,940),
                 (3,19,34,2529),(4,9,20,256)]
        for slot,row,col,height in samples:
            road=RoadSurface(raw,slot,verify_identity=False)
            with self.subTest(slot=slot):
                hit=road.sample_local(row,col,256,256)
                self.assertIsNotNone(hit)
                self.assertEqual(hit.height,height)
        self.assertEqual(path.read_bytes(),raw)
        self.assertEqual(path.stat().st_mtime_ns,before)


if __name__=='__main__':
    unittest.main()
