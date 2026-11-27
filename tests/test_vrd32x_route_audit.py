import unittest
from types import SimpleNamespace
from tools.vrd32x_route_audit import audit_route, segment_points


class Surface:
    def __init__(self, fn):
        self.fn = fn

    def sample_world(self, x, z):
        height = self.fn(x, z)
        return None if height is None else SimpleNamespace(height=height)


class RouteAuditTests(unittest.TestCase):
    def test_dense_samples_detect_gap_between_quarters(self):
        road = Surface(lambda x, z: None if (x, z) == (3, 0) else 0)
        result = audit_route([(0, 0), (8, 0), (8, 8)], road)
        self.assertEqual(result['unsupported_samples'], 1)
        self.assertEqual(result['affected_segments'], [0])
        self.assertEqual(result['unsupported_runs'][0]['first'], [3, 0])
        self.assertEqual(result['unsupported_runs'][0]['samples'], 1)

    def test_closing_segment_is_checked(self):
        road = Surface(lambda x, z: None if (x, z) == (3, 3) else 0)
        result = audit_route([(0, 0), (8, 0), (8, 8)], road)
        self.assertEqual(result['affected_segments'], [2])
        self.assertEqual(result['unsupported_samples'], 1)

    def test_two_heights_are_not_merged(self):
        normal = Surface(lambda x, z: 100)
        upper = Surface(lambda x, z: 1100 if (x, z) == (2, 0) else None)
        result = audit_route([(0, 0), (4, 0), (4, 4)], normal, upper)
        self.assertEqual(result['overlap_samples'], 1)
        self.assertEqual(result['maximum_overlap_height_delta_raw'], 1000)
        self.assertEqual(result['unsupported_samples'], 0)
        self.assertFalse(result['original_start_finish_verified'])

    def test_alternate_only_is_valid(self):
        normal = Surface(lambda x, z: None)
        upper = Surface(lambda x, z: 500)
        result = audit_route([(0, 0), (4, 0), (4, 4)], normal, upper)
        self.assertEqual(result['alternate_only_samples'], result['samples'])
        self.assertEqual(result['unsupported_samples'], 0)

    def test_segment_is_reversible_and_dense(self):
        forward = list(segment_points((0, -2), (8, 1)))
        backward = list(segment_points((8, 1), (0, -2)))
        self.assertEqual(forward, list(reversed(backward)))
        self.assertEqual(len(forward), 9)
        self.assertEqual(forward[0], (0, -2))
        self.assertEqual(forward[-1], (8, 1))
        self.assertTrue(all(max(abs(x-u), abs(z-v)) == 1
                            for (x,z),(u,v) in zip(forward,forward[1:])))

    def test_repeated_vertices_rejected(self):
        with self.assertRaisesRegex(ValueError, 'zero-length'):
            audit_route([(0, 0), (0, 0), (4, 4)], Surface(lambda x,z: 0))


if __name__ == '__main__':
    unittest.main()
