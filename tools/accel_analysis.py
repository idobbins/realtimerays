#!/usr/bin/env python3
"""Analytical acceleration-structure cost model for the realtime rays scene.

This is deliberately not a benchmark. It estimates traversal work and primitive
checks from scene density, sphere radius statistics, and ray-footprint area.
Use it to compare acceleration-structure families before implementing them.
"""

from __future__ import annotations

import argparse
import dataclasses
import math
import pathlib
import re
from typing import Callable, Iterable


@dataclasses.dataclass(frozen=True)
class Scene:
    sphere_count: int
    disk_radius: float
    min_radius: float
    max_radius: float
    bvh_leaf_size: int
    camera_pitch: float

    @property
    def disk_area(self) -> float:
        return math.pi * self.disk_radius * self.disk_radius

    @property
    def density(self) -> float:
        return self.sphere_count / self.disk_area

    @property
    def mean_radius(self) -> float:
        return 0.5 * (self.min_radius + self.max_radius)

    @property
    def mean_radius2(self) -> float:
        a = self.min_radius
        b = self.max_radius
        return (a * a + a * b + b * b) / 3.0

    @property
    def projected_coverage(self) -> float:
        return self.density * math.pi * self.mean_radius2


@dataclasses.dataclass(frozen=True)
class Scenario:
    name: str
    description: str
    footprint_area: float
    footprint_perimeter: float

    def expected_true_hits(self, scene: Scene) -> float:
        return min(float(scene.sphere_count), scene.density * self.footprint_area)


@dataclasses.dataclass(frozen=True)
class Cost:
    name: str
    traversal: float
    spheres: float
    total: float
    build: str
    memory: str
    note: str


def parse_float_constant(text: str, name: str, fallback: float) -> float:
    pattern = rf"^\s*#define\s+{re.escape(name)}\s+([0-9.+\-eE]+)f?u?\s*$"
    match = re.search(pattern, text, re.MULTILINE)
    return float(match.group(1)) if match else fallback


def parse_int_constant(text: str, name: str, fallback: int) -> int:
    define_pattern = rf"^\s*#define\s+{re.escape(name)}\s+([0-9]+)u?\s*$"
    define_match = re.search(define_pattern, text, re.MULTILINE)
    if define_match:
        return int(define_match.group(1))

    enum_pattern = rf"\b{re.escape(name)}\s*=\s*([0-9]+)"
    enum_match = re.search(enum_pattern, text)
    return int(enum_match.group(1)) if enum_match else fallback


def load_scene(repo: pathlib.Path) -> Scene:
    scene_c = (repo / "scene.c").read_text()
    macos_m = (repo / "macos.m").read_text() if (repo / "macos.m").exists() else ""
    vulkan_c = (repo / "vulkan.c").read_text() if (repo / "vulkan.c").exists() else ""
    camera_text = macos_m + "\n" + vulkan_c

    return Scene(
        sphere_count=parse_int_constant(scene_c, "RTR_SCENE_SPHERE_COUNT", 10000),
        disk_radius=parse_float_constant(scene_c, "RTR_SCENE_DISK_RADIUS", 2.60),
        min_radius=parse_float_constant(scene_c, "RTR_SCENE_MIN_SPHERE_RADIUS", 0.012),
        max_radius=parse_float_constant(scene_c, "RTR_SCENE_MAX_SPHERE_RADIUS", 0.025),
        bvh_leaf_size=parse_int_constant(scene_c, "RTR_BVH_LEAF_SIZE", 8),
        camera_pitch=parse_float_constant(camera_text, "RTR_CAMERA_DEFAULT_PITCH", 0.473),
    )


def ellipse_perimeter(a: float, b: float) -> float:
    if a <= 0.0 or b <= 0.0:
        return 0.0
    h = ((a - b) * (a - b)) / ((a + b) * (a + b))
    return math.pi * (a + b) * (1.0 + 3.0 * h / (10.0 + math.sqrt(4.0 - 3.0 * h)))


def plane_crossing_scenario(scene: Scene, name: str, mu: float, description: str) -> Scenario:
    """Ray intersects the center plane once.

    mu is abs(ray.y). A sphere hit footprint on the plane is an ellipse with
    semiaxes r / mu and r. This matches camera rays looking down at a sphere
    field standing on the ground plane.
    """

    mu = max(mu, 0.02)
    a = scene.mean_radius / mu
    b = scene.mean_radius
    area = math.pi * scene.mean_radius2 / mu
    return Scenario(
        name=name,
        description=description,
        footprint_area=area,
        footprint_perimeter=ellipse_perimeter(a, b),
    )


def planar_chord_scenario(scene: Scene, name: str, length: float, description: str) -> Scenario:
    """Ray travels in the sphere plane through a chord of the disk."""

    area = 2.0 * scene.mean_radius * length + math.pi * scene.mean_radius2
    perimeter = 2.0 * length + 2.0 * math.pi * scene.mean_radius
    return Scenario(
        name=name,
        description=description,
        footprint_area=area,
        footprint_perimeter=perimeter,
    )


def capped_area(scene: Scene, area: float) -> float:
    return min(scene.disk_area, max(0.0, area))


def dilated_area(scene: Scene, scenario: Scenario, pad: float) -> float:
    return capped_area(
        scene,
        scenario.footprint_area
        + scenario.footprint_perimeter * max(0.0, pad)
        + math.pi * max(0.0, pad) * max(0.0, pad),
    )


def score(traversal: float, spheres: float, traversal_cost: float, sphere_cost: float) -> float:
    return traversal * traversal_cost + spheres * sphere_cost


def fmt_count(value: float) -> str:
    if value >= 1000.0:
        return f"{value:8.0f}"
    if value >= 100.0:
        return f"{value:8.1f}"
    return f"{value:8.2f}"


def uniform_grid(
    scene: Scene,
    scenario: Scenario,
    traversal_cost: float,
    sphere_cost: float,
    hashed: bool,
) -> Cost:
    best: tuple[float, float, float, float, int] | None = None
    lo = max(scene.min_radius * 0.35, 0.001)
    hi = scene.disk_radius
    for i in range(180):
        t = i / 179.0
        h = lo * (hi / lo) ** t
        dim = max(1, math.ceil((scene.disk_radius * 2.0) / h))
        h = (scene.disk_radius * 2.0) / dim
        pad = 0.7071 * h
        area = dilated_area(scene, scenario, pad)
        touched_cells = max(1.0, area / (h * h))
        if hashed:
            touched_cells *= 1.15
        spheres = min(float(scene.sphere_count), scene.density * area)
        total = score(touched_cells, spheres, traversal_cost, sphere_cost)
        if best is None or total < best[0]:
            best = (total, touched_cells, spheres, h, dim)

    assert best is not None
    total, touched_cells, spheres, h, dim = best
    dense_cells = dim * dim
    occupied_disk_cells = min(dense_cells, int(math.ceil(scene.disk_area / (h * h))))
    avg_occ = scene.sphere_count / max(1, occupied_disk_cells)
    if hashed:
        name = "spatial hash grid"
        memory = f"~{occupied_disk_cells} occupied buckets"
        note = f"best h={h:.4f}, avg_occ={avg_occ:.2f}, extra hash ALU"
    else:
        name = "uniform 2D grid"
        memory = f"{dense_cells} cells"
        note = f"best h={h:.4f}, avg_occ={avg_occ:.2f}"
    return Cost(name, touched_cells, spheres, total, "O(N)", memory, note)


def strict_two_level_grid(
    scene: Scene,
    scenario: Scenario,
    traversal_cost: float,
    sphere_cost: float,
) -> Cost:
    """Fixed two-level 2D grid: coarse cells plus fixed-ratio fine subgrids."""

    best: tuple[float, float, float, float, int, int, int] | None = None
    lo = max(scene.min_radius * 0.35, 0.001)
    hi = scene.disk_radius
    for ratio in (4, 8, 16):
        for i in range(180):
            t = i / 179.0
            fine_h = lo * (hi / lo) ** t
            coarse_h = fine_h * ratio
            coarse_dim = max(1, math.ceil((scene.disk_radius * 2.0) / coarse_h))
            coarse_h = (scene.disk_radius * 2.0) / coarse_dim
            fine_h = coarse_h / ratio

            fine_area = dilated_area(scene, scenario, 0.7071 * fine_h)
            coarse_area = dilated_area(scene, scenario, 0.7071 * coarse_h)
            fine_cells = max(1.0, fine_area / (fine_h * fine_h))
            coarse_cells = max(1.0, coarse_area / (coarse_h * coarse_h))
            traversal = coarse_cells + fine_cells
            spheres = min(float(scene.sphere_count), scene.density * fine_area)
            total = score(traversal, spheres, traversal_cost, sphere_cost)
            if best is None or total < best[0]:
                best = (total, traversal, spheres, fine_h, ratio, coarse_dim, coarse_cells)

    assert best is not None
    total, traversal, spheres, fine_h, ratio, coarse_dim, coarse_cells = best
    coarse_cells_total = coarse_dim * coarse_dim
    occupied_coarse = min(
        coarse_cells_total,
        int(math.ceil(scene.disk_area / ((fine_h * ratio) * (fine_h * ratio)))),
    )
    fine_cells_total = occupied_coarse * ratio * ratio
    avg_occ = scene.sphere_count / max(1, fine_cells_total)
    return Cost(
        "strict 2-level grid",
        traversal,
        spheres,
        total,
        "O(N)",
        f"~{coarse_cells_total}+{fine_cells_total} cells",
        (
            f"fine_h={fine_h:.4f}, ratio={ratio}, coarse_checks={coarse_cells:.2f}, "
            f"avg_fine_occ={avg_occ:.2f}"
        ),
    )


def hgrid(scene: Scene, scenario: Scenario, traversal_cost: float, sphere_cost: float) -> Cost:
    levels: list[tuple[float, float, int]] = []
    r0 = scene.min_radius
    while r0 < scene.max_radius * 1.001:
        r1 = min(scene.max_radius, r0 * 2.0)
        width = r1 - r0
        total_width = scene.max_radius - scene.min_radius
        count = max(0, round(scene.sphere_count * width / total_width))
        if count:
            levels.append((r0, r1, count))
        r0 = r1
        if r0 >= scene.max_radius:
            break

    traversal = 0.0
    spheres = 0.0
    cells = 0
    for r_min, r_max, count in levels:
        density = count / scene.disk_area
        r_mean = 0.5 * (r_min + r_max)
        h = max(0.001, 2.0 * r_max)
        pad = 0.7071 * h
        area = dilated_area(scene, scenario, pad)
        traversal += max(1.0, area / (h * h))
        spheres += min(float(count), density * area)
        dim = max(1, math.ceil((scene.disk_radius * 2.0) / h))
        cells += min(dim * dim, int(math.ceil(scene.disk_area / (h * h))))
        _ = r_mean

    total = score(traversal, spheres, traversal_cost, sphere_cost)
    return Cost(
        "hierarchical grid",
        traversal,
        spheres,
        total,
        "O(N)",
        f"~{cells} occupied buckets",
        f"{len(levels)} radius levels; radius range is only {scene.max_radius / scene.min_radius:.1f}x",
    )


def leaf_size_models(
    scene: Scene,
    scenario: Scenario,
    traversal_cost: float,
    sphere_cost: float,
    name: str,
    leaf_sizes: Iterable[int],
    pad_factor: float,
    leaf_traversal_factor: float,
    depth_factor: float,
    build: str,
    memory_factor: float,
    note_for: Callable[[int, float], str],
) -> Cost:
    best: tuple[float, float, float, int, float] | None = None
    for leaf in leaf_sizes:
        leaf_area = leaf / scene.density
        h = math.sqrt(leaf_area)
        pad = pad_factor * h
        area = dilated_area(scene, scenario, pad)
        leaves = max(1.0, area / leaf_area)
        depth = math.log2(max(2.0, scene.sphere_count / leaf))
        traversal = depth_factor * depth + leaf_traversal_factor * leaves
        spheres = min(float(scene.sphere_count), scene.density * area)
        total = score(traversal, spheres, traversal_cost, sphere_cost)
        if best is None or total < best[0]:
            best = (total, traversal, spheres, leaf, h)

    assert best is not None
    total, traversal, spheres, leaf, h = best
    nodes = int(math.ceil((scene.sphere_count / leaf) * memory_factor))
    return Cost(name, traversal, spheres, total, build, f"~{nodes} nodes/leaves", note_for(leaf, h))


def bvh_model(
    scene: Scene,
    scenario: Scenario,
    traversal_cost: float,
    sphere_cost: float,
    name: str,
    pad_factor: float,
    leaf_traversal_factor: float,
    depth_factor: float,
    build: str,
    note: str,
) -> Cost:
    leaf = scene.bvh_leaf_size
    leaf_area = leaf / scene.density
    h = math.sqrt(leaf_area)
    area = dilated_area(scene, scenario, pad_factor * h)
    leaves = max(1.0, area / leaf_area)
    depth = math.log2(max(2.0, scene.sphere_count / leaf))
    traversal = depth_factor * depth + leaf_traversal_factor * leaves
    spheres = min(float(scene.sphere_count), scene.density * area)
    total = score(traversal, spheres, traversal_cost, sphere_cost)
    nodes = scene.sphere_count * 2 - 1
    return Cost(
        name,
        traversal,
        spheres,
        total,
        build,
        f"{nodes} nodes",
        f"leaf={leaf}, leaf_span~{h:.3f}; {note}",
    )


def costs_for(scene: Scene, scenario: Scenario, traversal_cost: float, sphere_cost: float) -> list[Cost]:
    costs: list[Cost] = []
    true_hits = scenario.expected_true_hits(scene)
    costs.append(
        Cost(
            "ideal lower bound",
            0.0,
            true_hits,
            score(0.0, true_hits, traversal_cost, sphere_cost),
            "n/a",
            "n/a",
            "expected true intersections only",
        )
    )
    costs.append(
        Cost(
            "brute force",
            0.0,
            float(scene.sphere_count),
            score(0.0, float(scene.sphere_count), traversal_cost, sphere_cost),
            "none",
            "sphere array",
            "closest hit must test all spheres",
        )
    )
    costs.append(
        Cost(
            "scene AABB + brute",
            1.0,
            float(scene.sphere_count),
            score(1.0, float(scene.sphere_count), traversal_cost, sphere_cost),
            "none",
            "1 box",
            "only helps rays that miss the disk bounds",
        )
    )
    costs.append(uniform_grid(scene, scenario, traversal_cost, sphere_cost, hashed=False))
    costs.append(uniform_grid(scene, scenario, traversal_cost, sphere_cost, hashed=True))
    costs.append(strict_two_level_grid(scene, scenario, traversal_cost, sphere_cost))
    costs.append(hgrid(scene, scenario, traversal_cost, sphere_cost))
    costs.append(
        leaf_size_models(
            scene,
            scenario,
            traversal_cost,
            sphere_cost,
            "loose quadtree",
            (4, 8, 16, 32),
            pad_factor=0.50,
            leaf_traversal_factor=1.35,
            depth_factor=0.95,
            build="O(N log N)",
            memory_factor=1.35,
            note_for=lambda leaf, h: f"best leaf={leaf}, cell_span~{h:.3f}; adaptive 2D tree",
        )
    )
    costs.append(
        leaf_size_models(
            scene,
            scenario,
            traversal_cost,
            sphere_cost,
            "linear quadtree",
            (4, 8, 16, 32),
            pad_factor=0.62,
            leaf_traversal_factor=1.50,
            depth_factor=0.90,
            build="O(N log N)",
            memory_factor=1.20,
            note_for=lambda leaf, h: f"best leaf={leaf}, cell_span~{h:.3f}; Morton sorted leaves",
        )
    )
    costs.append(
        leaf_size_models(
            scene,
            scenario,
            traversal_cost,
            sphere_cost,
            "octree",
            (4, 8, 16, 32),
            pad_factor=0.70,
            leaf_traversal_factor=1.85,
            depth_factor=1.10,
            build="O(N log N)",
            memory_factor=1.80,
            note_for=lambda leaf, h: f"best leaf={leaf}, cube_span~{h:.3f}; weak fit for thin plane data",
        )
    )
    costs.append(
        leaf_size_models(
            scene,
            scenario,
            traversal_cost,
            sphere_cost,
            "spatial kd-tree",
            (4, 8, 16, 32),
            pad_factor=0.30,
            leaf_traversal_factor=1.15,
            depth_factor=1.00,
            build="O(N log N)",
            memory_factor=1.60,
            note_for=lambda leaf, h: f"best leaf={leaf}, cell_span~{h:.3f}; stronger empty-space cuts",
        )
    )
    costs.append(
        bvh_model(
            scene,
            scenario,
            traversal_cost,
            sphere_cost,
            "median BVH",
            pad_factor=0.45,
            leaf_traversal_factor=1.30,
            depth_factor=1.05,
            build="O(N log N)",
            note="simple splits, more overlap",
        )
    )
    costs.append(
        bvh_model(
            scene,
            scenario,
            traversal_cost,
            sphere_cost,
            "LBVH / Morton BVH",
            pad_factor=0.55,
            leaf_traversal_factor=1.45,
            depth_factor=0.95,
            build="O(N)",
            note="fast build, lower quality",
        )
    )
    costs.append(
        bvh_model(
            scene,
            scenario,
            traversal_cost,
            sphere_cost,
            "binned SAH BVH",
            pad_factor=0.18,
            leaf_traversal_factor=1.10,
            depth_factor=1.00,
            build="O(N log N)",
            note="matches current scene.c builder",
        )
    )
    costs.append(
        bvh_model(
            scene,
            scenario,
            traversal_cost,
            sphere_cost,
            "wide BVH4",
            pad_factor=0.22,
            leaf_traversal_factor=1.35,
            depth_factor=0.62,
            build="O(N log N)",
            note="shallower tree, more child boxes per node",
        )
    )
    return sorted(costs, key=lambda c: c.total)


def print_scenario(scene: Scene, scenario: Scenario, costs: list[Cost]) -> None:
    print()
    print(f"Scenario: {scenario.name}")
    print(f"  {scenario.description}")
    print(
        "  footprint_area={:.6f}, perimeter={:.4f}, expected_true_hits={:.3f}".format(
            scenario.footprint_area,
            scenario.footprint_perimeter,
            scenario.expected_true_hits(scene),
        )
    )
    print()
    print(
        "  {:<22} {:>8} {:>8} {:>9}  {:<10} {:<18} {}".format(
            "structure", "trav", "spheres", "score", "build", "memory", "note"
        )
    )
    print("  " + "-" * 116)
    for cost in costs:
        print(
            "  {:<22} {} {} {:9.2f}  {:<10} {:<18} {}".format(
                cost.name,
                fmt_count(cost.traversal),
                fmt_count(cost.spheres),
                cost.total,
                cost.build,
                cost.memory,
                cost.note,
            )
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[1],
        help="repository root containing scene.c",
    )
    parser.add_argument(
        "--traversal-cost",
        type=float,
        default=1.0,
        help="relative cost of one cell/node/box check",
    )
    parser.add_argument(
        "--sphere-cost",
        type=float,
        default=4.0,
        help="relative cost of one sphere intersection check",
    )
    args = parser.parse_args()

    scene = load_scene(args.repo)
    camera_mu = abs(math.sin(scene.camera_pitch))
    mean_chord = 4.0 * scene.disk_radius / math.pi

    scenarios = [
        plane_crossing_scenario(
            scene,
            "default primary camera",
            camera_mu,
            f"center-ish camera ray, mu=abs(sin({scene.camera_pitch:.3f}))={camera_mu:.3f}",
        ),
        plane_crossing_scenario(
            scene,
            "shallow secondary",
            0.15,
            "grazing bounce / low vertical component; the footprint stretches along the ray",
        ),
        plane_crossing_scenario(
            scene,
            "shadow-to-light-ish",
            0.80,
            "steep ray toward the current elevated point light",
        ),
        planar_chord_scenario(
            scene,
            "planar mean chord",
            mean_chord,
            f"ray travels in the sphere plane for the mean disk chord, L={mean_chord:.3f}",
        ),
        planar_chord_scenario(
            scene,
            "planar diameter",
            2.0 * scene.disk_radius,
            f"worst common horizontal chord through the whole disk, L={2.0 * scene.disk_radius:.3f}",
        ),
    ]

    print("Realtime Rays analytical acceleration-structure model")
    print(f"  repo: {args.repo}")
    print(
        "  scene: N={}, disk_R={:.3f}, radius=[{:.4f}, {:.4f}], leaf={}".format(
            scene.sphere_count,
            scene.disk_radius,
            scene.min_radius,
            scene.max_radius,
            scene.bvh_leaf_size,
        )
    )
    print(
        "  density={:.2f}/unit^2, mean_r={:.5f}, mean_r2={:.7f}, projected_coverage={:.3f}".format(
            scene.density,
            scene.mean_radius,
            scene.mean_radius2,
            scene.projected_coverage,
        )
    )
    print(
        "  score = traversal * {:.2f} + sphere_checks * {:.2f}".format(
            args.traversal_cost,
            args.sphere_cost,
        )
    )
    print("  lower bound is expected true intersections; other rows include false positives.")

    for scenario in scenarios:
        print_scenario(
            scene,
            scenario,
            costs_for(scene, scenario, args.traversal_cost, args.sphere_cost),
        )

    print()
    print("Model cautions:")
    print("  - These are first-order analytical estimates, not measured GPU timings.")
    print("  - Coherent rays can make BVHs look better than this scalar model.")
    print("  - Any-hit shadow rays can terminate earlier than the closest-hit rows above.")
    print("  - The current radius range is narrow, so hierarchical grids have little to exploit.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
