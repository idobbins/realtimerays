#!/usr/bin/env python3
"""First-principles ray throughput limits for the packed scene data layout.

The model asks: if bandwidth is the ceiling, how many bytes must each ray touch?
It reuses the analytical candidate counts from accel_analysis.py and reports the
single canonical packed fixed-point layout used by the renderer.
"""

from __future__ import annotations

import argparse
import dataclasses
import math
import pathlib

from accel_analysis import (
    costs_for,
    load_scene,
    planar_chord_scenario,
    plane_crossing_scenario,
)


@dataclasses.dataclass(frozen=True)
class Layout:
    name: str
    node_bytes: float
    sphere_bytes: float
    cell_bytes: float
    ray_roundtrip_bytes: float
    node_ops: float
    sphere_ops: float
    cell_ops: float
    note: str


@dataclasses.dataclass(frozen=True)
class Profile:
    name: str
    nodes: float
    cells: float
    spheres: float
    note: str


def find_cost(costs, name: str):
    for cost in costs:
        if cost.name == name:
            return cost
    raise KeyError(name)


def fmt_rate(rays_per_second: float) -> str:
    if rays_per_second >= 1.0e9:
        return f"{rays_per_second / 1.0e9:7.2f}B"
    if rays_per_second >= 1.0e6:
        return f"{rays_per_second / 1.0e6:7.2f}M"
    return f"{rays_per_second:7.0f}"


def fmt_bytes(value: float) -> str:
    if value >= 1024.0:
        return f"{value / 1024.0:7.2f}KB"
    return f"{value:7.1f}B"


def make_profiles(scene) -> list[Profile]:
    camera_mu = abs(math.sin(scene.camera_pitch))
    mean_chord = 4.0 * scene.disk_radius / math.pi
    scenarios = [
        (
            "default primary",
            plane_crossing_scenario(scene, "default primary", camera_mu, ""),
        ),
        (
            "shallow secondary",
            plane_crossing_scenario(scene, "shallow secondary", 0.15, ""),
        ),
        (
            "planar diameter",
            planar_chord_scenario(scene, "planar diameter", 2.0 * scene.disk_radius, ""),
        ),
    ]

    profiles: list[Profile] = [
        Profile(
            "brute force",
            nodes=0.0,
            cells=0.0,
            spheres=float(scene.sphere_count),
            note="all spheres",
        )
    ]

    for label, scenario in scenarios:
        costs = costs_for(scene, scenario, traversal_cost=1.0, sphere_cost=4.0)
        ideal = find_cost(costs, "ideal lower bound")
        grid = find_cost(costs, "uniform 2D grid")
        two_level = find_cost(costs, "strict 2-level grid")
        bvh = find_cost(costs, "binned SAH BVH")

        profiles.extend(
            [
                Profile(
                    f"{label} ideal oracle",
                    nodes=0.0,
                    cells=0.0,
                    spheres=ideal.spheres,
                    note="true hit records only",
                ),
                Profile(
                    f"{label} grid",
                    nodes=0.0,
                    cells=grid.traversal,
                    spheres=grid.spheres,
                    note="flat 2D grid",
                ),
                Profile(
                    f"{label} 2-level grid",
                    nodes=0.0,
                    cells=two_level.traversal,
                    spheres=two_level.spheres,
                    note="strict fixed two-level grid",
                ),
                Profile(
                    f"{label} bvh",
                    nodes=bvh.traversal,
                    cells=0.0,
                    spheres=bvh.spheres,
                    note="current binned SAH shape",
                ),
            ]
        )

    return profiles


def make_layouts(ray_queue: bool) -> list[Layout]:
    ray1 = 16.0 if ray_queue else 0.0
    return [
        Layout(
            "packed fixed 3D",
            node_bytes=16.0,
            sphere_bytes=4.0,
            cell_bytes=4.0,
            ray_roundtrip_bytes=ray1,
            node_ops=50.0,
            sphere_ops=55.0,
            cell_ops=5.0,
            note="4B sphere, procedural color, 16B quantized 3D node",
        ),
    ]


def bytes_per_ray(profile: Profile, layout: Layout) -> float:
    return (
        profile.nodes * layout.node_bytes
        + profile.cells * layout.cell_bytes
        + profile.spheres * layout.sphere_bytes
        + layout.ray_roundtrip_bytes
    )


def ops_per_ray(profile: Profile, layout: Layout) -> float:
    return (
        profile.nodes * layout.node_ops
        + profile.cells * layout.cell_ops
        + profile.spheres * layout.sphere_ops
    )


def print_layout_notes(layouts: list[Layout]) -> None:
    print("Layouts:")
    for layout in layouts:
        ray = (
            f", ray queue roundtrip {layout.ray_roundtrip_bytes:.0f}B"
            if layout.ray_roundtrip_bytes
            else ""
        )
        print(f"  {layout.name:<18} {layout.note}{ray}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[1],
        help="repository root containing scene.c",
    )
    parser.add_argument(
        "--bandwidth-gb-s",
        type=float,
        default=200.0,
        help="sustained memory bandwidth to model",
    )
    parser.add_argument(
        "--fp32-tflops",
        type=float,
        default=5.3,
        help="FP32-equivalent ALU throughput to model",
    )
    parser.add_argument(
        "--ray-queue",
        action="store_true",
        help="include one ray-state read/write roundtrip per query",
    )
    args = parser.parse_args()

    scene = load_scene(args.repo)
    bandwidth = args.bandwidth_gb_s * 1.0e9
    flops = args.fp32_tflops * 1.0e12
    layouts = make_layouts(args.ray_queue)
    profiles = make_profiles(scene)

    print("Packed fixed-point throughput model")
    print(f"  scene: N={scene.sphere_count}, disk_R={scene.disk_radius:.3f}")
    print(f"  bandwidth={args.bandwidth_gb_s:.1f} GB/s, ALU={args.fp32_tflops:.2f} TFLOP/s")
    if args.ray_queue:
        print("  including ray-state traffic for a wavefront/persistent-ray design")
    else:
        print("  ray state traffic excluded, matching a megakernel that keeps rays in registers")
    print()
    print_layout_notes(layouts)
    print()

    for profile in profiles:
        print(profile.name)
        print(
            "  touches: nodes={:.2f}, cells={:.2f}, spheres={:.2f} ({})".format(
                profile.nodes,
                profile.cells,
                profile.spheres,
                profile.note,
            )
        )
        print("  {:<18} {:>9} {:>10} {:>10} {:>10}".format(
            "layout", "bytes/ray", "BW cap", "ALU cap", "limit"
        ))
        for layout in layouts:
            bpr = bytes_per_ray(profile, layout)
            opr = max(1.0, ops_per_ray(profile, layout))
            bw_cap = bandwidth / max(1.0, bpr)
            alu_cap = flops / opr
            limit = min(bw_cap, alu_cap)
            print(
                "  {:<18} {:>9} {:>10} {:>10} {:>10}".format(
                    layout.name,
                    fmt_bytes(bpr),
                    fmt_rate(bw_cap),
                    fmt_rate(alu_cap),
                    fmt_rate(limit),
                )
            )
        print()

    print("Interpretation:")
    print("  - Sphere geometry and BVH nodes are the canonical packed runtime layout.")
    print("  - Sphere colors are reconstructed procedurally; there is no material table.")
    print("  - Once candidate data falls below ~50B/ray, ray queue traffic starts to matter.")
    print("  - Quantized node bounds are inflated enough to cover quantization error.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
