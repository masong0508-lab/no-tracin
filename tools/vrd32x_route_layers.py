"""Source-only closed-route layer choice. No donor assets or guessed surfaces."""


def choose_closed_layers(heights):
    """Minimize height variation, then layer changes, including the closing edge."""
    if not heights or any(len(h) != 2 or all(v is None for v in h) for h in heights):
        raise ValueError('unsupported two-layer route sample')
    best = None
    for start, height in enumerate(heights[0]):
        if height is None:
            continue
        costs = {start: (0, 0)}
        parents = []
        for prev, cur in zip(heights, heights[1:]):
            next_cost, parent = {}, {}
            for layer, z in enumerate(cur):
                if z is None:
                    continue
                cost, switches, old = min(
                    (cost + abs(z - prev[old]), switches + (old != layer), old)
                    for old, (cost, switches) in costs.items())
                next_cost[layer] = (cost, switches)
                parent[layer] = old
            costs = next_cost
            parents.append(parent)
        cost, switches, end = min(
            (cost + abs(heights[-1][old] - height), switches + (old != start), old)
            for old, (cost, switches) in costs.items())
        path = [end]
        for parent in reversed(parents):
            path.append(parent[path[-1]])
        path.reverse()
        candidate = ((cost, switches, start), path)
        if best is None or candidate[0] < best[0]:
            best = candidate
    return best[1]


def source_route_layers(points, normal, alternate):
    """Return source layer at every route node using dense native samples.

    The choice is a continuity policy for the importer, not recovered Sega
    race rules or proof of the live player's collision-layer behavior.
    """
    if alternate is None:
        return [0] * len(points)
    heights, nodes = [], []
    for a, b in zip(points, points[1:] + points[:1]):
        nodes.append(len(heights))
        dx, dz = b[0] - a[0], b[1] - a[1]
        steps = max(abs(dx), abs(dz))
        if not steps:
            raise ValueError('zero-length route segment')
        for i in range(steps):
            x = (2 * (a[0] * steps + dx * i) + steps) // (2 * steps)
            z = (2 * (a[1] * steps + dz * i) + steps) // (2 * steps)
            lo, hi = normal.sample_world(x, z), alternate.sample_world(x, z)
            heights.append((None if lo is None else lo.height,
                            None if hi is None else hi.height))
    layers = choose_closed_layers(heights)
    return [layers[i] for i in nodes]
