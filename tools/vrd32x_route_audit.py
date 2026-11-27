"""Dense private audit of original 32X route vs native collision surfaces.

Checks a rounded native integer-lattice centerline, not the full swept car
footprint, original racing rules, or SH-2 visual alignment. No donor data
is embedded. This diagnostic does not change game physics or build assets.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def segment_points(a, b):
    """Sample each dominant-axis integer step, including both endpoints."""
    dx, dz = b[0] - a[0], b[1] - a[1]
    steps = max(abs(dx), abs(dz))
    if not steps:
        yield tuple(a)
        return
    for i in range(steps + 1):
        # Round to nearest integer, ties toward +infinity, independent of
        # travel direction. Avoid float precision and signed division traps.
        yield tuple((2 * (p * steps + delta * i) + steps) // (2 * steps)
                    for p, delta in zip(a, (dx, dz)))


def audit_route(points, normal, alternate=None):
    """Count uncovered samples and preserve both heights at overlaps."""
    points = [tuple(p) for p in points]
    if len(points) > 1 and points[0] == points[-1]:
        points.pop()  # explicit closed-loop representation
    if len(points) < 3:
        raise ValueError('at least three route vertices required')
    for a, b in zip(points, points[1:] + points[:1]):
        if a == b:
            raise ValueError('zero-length source route segment')
    report = dict(samples=0, unsupported_samples=0, normal_only_samples=0,
                  alternate_only_samples=0, overlap_samples=0,
                  maximum_overlap_height_delta_raw=None,
                  affected_segments=[], unsupported_runs=[],
                  original_start_finish_verified=False,
                  original_race_rules_verified=False,
                  full_vehicle_footprint_verified=False)
    for index, (a, b) in enumerate(zip(points, points[1:] + points[:1])):
        run = None
        # The following segment owns the shared endpoint, including closure.
        for x, z in list(segment_points(a, b))[:-1]:
            report['samples'] += 1
            lo = normal.sample_world(x, z)
            hi = alternate.sample_world(x, z) if alternate is not None else None
            if lo is None and hi is None:
                report['unsupported_samples'] += 1
                if run is None:
                    run = dict(segment=index, first=[x,z], last=[x,z], samples=0)
                    report['unsupported_runs'].append(run)
                    if not report['affected_segments'] or report['affected_segments'][-1] != index:
                        report['affected_segments'].append(index)
                run['last'] = [x,z]
                run['samples'] += 1
                continue
            run = None
            if lo is not None and hi is not None:
                report['overlap_samples'] += 1
                delta = abs(lo.height - hi.height)
                previous = report['maximum_overlap_height_delta_raw']
                report['maximum_overlap_height_delta_raw'] = delta if previous is None else max(previous, delta)
            elif lo is not None:
                report['normal_only_samples'] += 1
            else:
                report['alternate_only_samples'] += 1
    return report


def main():
    from tools.decode_vrd_32x_surface import RoadSurface
    from tools.vrd32x_native_routes import ordered_source_points

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('build/vrd32x-route-audit.json'))
    parser.add_argument('--strict', action='store_true', help='exit nonzero if a sampled route gap is found')
    args = parser.parse_args()
    image = args.rom.read_bytes()
    reports = []
    for slot in range(5):
        points, evidence = ordered_source_points(image, slot)
        normal = RoadSurface(image, slot, verify_identity=False)
        alternate = RoadSurface(image, slot, True, verify_identity=False) if slot == 3 else None
        report = audit_route(points, normal, alternate)
        reports.append(dict(course=evidence['original_course_name'], slot=slot, **report))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(reports, indent=2) + '\n', encoding='utf-8')
    for report in reports:
        print(f"{report['course']}: {report['samples']} samples, "
              f"{report['unsupported_samples']} unsupported, "
              f"{report['overlap_samples']} two-layer samples")
    return int(args.strict and any(r['unsupported_samples'] for r in reports))


if __name__ == '__main__':
    raise SystemExit(main())
