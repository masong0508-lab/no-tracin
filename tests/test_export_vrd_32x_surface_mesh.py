"""Reconstructed road OBJ mesh is derived offline, bounded and deterministic."""
from pathlib import Path
import os
import tempfile
import unittest
from tools.export_vrd_32x_surface_mesh import surface_faces, write_obj, grid_samples
from tools.decode_vrd_32x_surface import RoadSurface


class SurfaceMeshTests(unittest.TestCase):
    def test_faces_require_four_valid_road_vertices(self):
        data=[10,20,None,30,40,None,None,None,None]
        self.assertEqual(list(surface_faces(3,data)), [((0,3,1),(3,4,1))])
        self.assertFalse(list(surface_faces(3,[10]+[None]*8)))

    def test_local_obj_export_is_deterministic_and_no_background(self):
        heights=[10,20,None,30,40,None,None,None,None]
        with tempfile.TemporaryDirectory() as tmp:
            a=Path(tmp)/'a.obj';b=Path(tmp)/'b.obj'
            one=write_obj(a,3,heights,vertical_divisor=32)
            two=write_obj(b,3,heights,vertical_divisor=32)
            self.assertEqual(one,two)
            self.assertEqual(one['vertices'],4)
            self.assertEqual(one['triangles'],2)
            self.assertEqual(a.read_bytes(),b.read_bytes())
            lines=a.read_text().splitlines()
            self.assertEqual(len([x for x in lines if x.startswith('v ')]),4)
            self.assertEqual(len([x for x in lines if x.startswith('f ')]),2)
            self.assertIn('0.3125',lines[3])
            self.assertRaisesRegex(ValueError,'invalid',write_obj,a,3,heights,vertical_divisor=0)

    def test_invalid_sampling_resolution_rejected(self):
        class EmptyRoad:
            grid=[0]*4096
            background=0
        for bad in (0,3,17):
            with self.assertRaisesRegex(ValueError,'samples per'):grid_samples(EmptyRoad(),bad)

    def test_exact_owned_rom_has_bounded_reconstruction(self):
        path=os.environ.get('VR32X_ROM')
        if not path:self.skipTest('Set VR32X_ROM to validate private mesh reconstruction')
        rom=Path(path).read_bytes()
        expected=[(10930,19632),(11906,21136),(9973,16936),(8093,14028),(20561,37244)]
        with tempfile.TemporaryDirectory() as temp:
            for i,(v,f) in enumerate(expected):
                road=RoadSurface(rom,i,verify_identity=False)
                side,heights=grid_samples(road,8)
                self.assertEqual(side,512)
                self.assertEqual(sum(x is not None for x in heights),v)
                obj=Path(temp)/f'{i}.obj'
                result=write_obj(obj,side,heights)
                self.assertEqual((result['vertices'],result['triangles']),(v,f))
                self.assertLess(obj.stat().st_size,2_000_000)


if __name__=='__main__':
    unittest.main()
