use realtimerays::scene;
use scene::{BuiltBvh, BvhBuildConfig, BvhSplit, Triangle, Vec3};
use std::time::Instant;

const FOCAL: f32 = 1.35;
const EPS: f32 = 0.001;
const INF: f32 = 1.0e20;
const LIGHT_CENTER: Vec3 = Vec3 {
    x: 0.0,
    y: 6.0,
    z: -2.8,
};
const EMISSION_EPS2: f32 = 0.0001;

#[derive(Clone, Copy, Default)]
struct Counts {
    rays: u64,
    hits: u64,
    nodes: u64,
    leaves: u64,
    triangles: u64,
}

#[derive(Clone, Copy)]
struct Hit {
    t: f32,
    n: Vec3,
    emission: Vec3,
}

fn main() {
    let (width, height) = parse_size();
    let scene_data = scene::load_scene_data();
    println!(
        "scene triangles={} rays={}x{}",
        scene_data.triangles().len(),
        width,
        height
    );
    println!(
        "{:<18} {:>4} {:>4} {:>8} {:>10} {:>10} {:>10} {:>10} {:>10}",
        "split", "leaf", "bins", "nodes", "node/ray", "tri/ray", "cost1", "cost4", "build_ms"
    );

    let mut configs = Vec::new();
    for leaf in [1usize, 2, 4, 6, 8, 12, 16] {
        configs.push(BvhBuildConfig {
            max_leaf_size: leaf,
            split: BvhSplit::Median,
            bins: 12,
        });
    }
    for bins in [8usize, 12, 16, 24] {
        for leaf in [1usize, 2, 4, 6, 8, 12, 16] {
            configs.push(BvhBuildConfig {
                max_leaf_size: leaf,
                split: BvhSplit::BinnedSah,
                bins,
            });
        }
    }

    let mut rows = Vec::new();
    for config in configs {
        let start = Instant::now();
        let bvh = scene::build_bvh(scene_data.triangles(), config);
        let build_ms = start.elapsed().as_secs_f64() * 1000.0;
        let counts = analyze(&bvh, width, height);
        rows.push((
            score(&counts, 4.0),
            config,
            bvh.nodes.len(),
            counts,
            build_ms,
        ));
    }

    rows.sort_by(|a, b| a.0.total_cmp(&b.0));
    for (_score, config, node_count, counts, build_ms) in rows {
        let rays = counts.rays.max(1) as f64;
        let node_per_ray = counts.nodes as f64 / rays;
        let tri_per_ray = counts.triangles as f64 / rays;
        let cost1 = score(&counts, 1.0) / rays;
        let cost4 = score(&counts, 4.0) / rays;
        println!(
            "{:<18} {:>4} {:>4} {:>8} {:>10.2} {:>10.2} {:>10.2} {:>10.2} {:>10.1}",
            split_name(config.split),
            config.max_leaf_size,
            config.bins,
            node_count,
            node_per_ray,
            tri_per_ray,
            cost1,
            cost4,
            build_ms
        );
    }
}

fn parse_size() -> (usize, usize) {
    let mut width = 160usize;
    let mut height = 90usize;
    let args: Vec<String> = std::env::args().collect();
    let mut i = 1usize;
    while i + 1 < args.len() {
        match args[i].as_str() {
            "--width" => width = args[i + 1].parse().unwrap_or(width),
            "--height" => height = args[i + 1].parse().unwrap_or(height),
            _ => {}
        }
        i += 2;
    }
    (width.max(1), height.max(1))
}

fn analyze(bvh: &BuiltBvh, width: usize, height: usize) -> Counts {
    let mut counts = Counts::default();
    for y in 0..height {
        for x in 0..width {
            let (ro, rd) = camera_ray(x, y, width, height);
            let (primary, primary_counts) = trace_closest(bvh, ro, rd);
            counts.add(primary_counts);
            let Some(primary) = primary else {
                continue;
            };
            if has_emission(primary.emission) {
                continue;
            }

            let hit_pos = ro.add(rd.mul(primary.t));
            counts.add(shadow_counts(bvh, hit_pos, primary.n, LIGHT_CENTER));

            let bounce_rd = cosine_hemisphere(primary.n, hash2(x as u32, y as u32));
            let bounce_ro = hit_pos.add(primary.n.mul(EPS * 2.0));
            let (bounce, bounce_counts) = trace_closest(bvh, bounce_ro, bounce_rd);
            counts.add(bounce_counts);
            if let Some(bounce) = bounce {
                if !has_emission(bounce.emission) {
                    let bounce_pos = bounce_ro.add(bounce_rd.mul(bounce.t));
                    counts.add(shadow_counts(bvh, bounce_pos, bounce.n, LIGHT_CENTER));
                }
            }
        }
    }
    counts
}

impl Counts {
    fn add(&mut self, rhs: Counts) {
        self.rays += rhs.rays;
        self.hits += rhs.hits;
        self.nodes += rhs.nodes;
        self.leaves += rhs.leaves;
        self.triangles += rhs.triangles;
    }
}

fn shadow_counts(bvh: &BuiltBvh, p: Vec3, n: Vec3, light_pos: Vec3) -> Counts {
    let to_light = light_pos.sub(p);
    let dist2 = to_light.dot(to_light);
    let inv_dist = 1.0 / dist2.sqrt();
    let rd = to_light.mul(inv_dist);
    let ro = p.add(n.mul(EPS * 2.0));
    trace_any(bvh, ro, rd, dist2 * inv_dist - 0.02).1
}

fn camera_ray(x: usize, y: usize, width: usize, height: usize) -> (Vec3, Vec3) {
    let angle: f32 = 0.42;
    let ro = Vec3::new(angle.sin() * 9.4, 4.2, angle.cos() * 9.4);
    let target = Vec3::new(0.0, 0.95, 0.0);
    let forward = target.sub(ro).normalize();
    let right = forward.cross(Vec3::new(0.0, 1.0, 0.0)).normalize();
    let up = right.cross(forward);
    let uvx = (x as f32 + 0.5) / width as f32;
    let uvy = (y as f32 + 0.5) / height as f32;
    let mut sx = uvx * 2.0 - 1.0;
    let sy = -(uvy * 2.0 - 1.0);
    sx *= width as f32 / height as f32;
    let rd = forward
        .mul(FOCAL)
        .add(right.mul(sx))
        .add(up.mul(sy))
        .normalize();
    (ro, rd)
}

fn trace_closest(bvh: &BuiltBvh, ro: Vec3, rd: Vec3) -> (Option<Hit>, Counts) {
    let mut counts = Counts {
        rays: 1,
        ..Counts::default()
    };
    let mut stack = Vec::with_capacity(64);
    stack.push(0usize);
    let inv_rd = Vec3::new(1.0 / rd.x, 1.0 / rd.y, 1.0 / rd.z);
    let mut hit_t = INF;
    let mut hit = None;

    while let Some(node_index) = stack.pop() {
        if node_index >= bvh.nodes.len() {
            continue;
        }
        let node = bvh.nodes[node_index];
        counts.nodes += 1;
        if aabb_near(ro, inv_rd, node.bmin, node.bmax, hit_t) == INF {
            continue;
        }

        if is_leaf(node.right_or_count) {
            counts.leaves += 1;
            let first = node.left_first as usize;
            let count = leaf_count(node.right_or_count);
            for tri_index in first..first + count {
                counts.triangles += 1;
                if let Some(candidate) =
                    intersect_triangle(&bvh.triangles[tri_index], ro, rd, hit_t)
                {
                    hit_t = candidate.t;
                    hit = Some(candidate);
                }
            }
        } else {
            stack.push(node.right_or_count as usize);
            stack.push(node.left_first as usize);
        }
    }

    if hit.is_some() {
        counts.hits = 1;
    }
    (hit, counts)
}

fn trace_any(bvh: &BuiltBvh, ro: Vec3, rd: Vec3, max_t: f32) -> (bool, Counts) {
    let mut counts = Counts {
        rays: 1,
        ..Counts::default()
    };
    let mut stack = Vec::with_capacity(64);
    stack.push(0usize);
    let inv_rd = Vec3::new(1.0 / rd.x, 1.0 / rd.y, 1.0 / rd.z);

    while let Some(node_index) = stack.pop() {
        if node_index >= bvh.nodes.len() {
            continue;
        }
        let node = bvh.nodes[node_index];
        counts.nodes += 1;
        if aabb_near(ro, inv_rd, node.bmin, node.bmax, max_t) == INF {
            continue;
        }

        if is_leaf(node.right_or_count) {
            counts.leaves += 1;
            let first = node.left_first as usize;
            let count = leaf_count(node.right_or_count);
            for tri_index in first..first + count {
                counts.triangles += 1;
                let tri = &bvh.triangles[tri_index];
                if has_emission(tri.emission) {
                    continue;
                }
                if intersect_triangle(tri, ro, rd, max_t).is_some() {
                    counts.hits = 1;
                    return (true, counts);
                }
            }
        } else {
            stack.push(node.right_or_count as usize);
            stack.push(node.left_first as usize);
        }
    }

    (false, counts)
}

fn intersect_triangle(tri: &Triangle, ro: Vec3, rd: Vec3, max_t: f32) -> Option<Hit> {
    let e1 = tri.v1.sub(tri.v0);
    let e2 = tri.v2.sub(tri.v0);
    let pvec = rd.cross(e2);
    let det = e1.dot(pvec);
    if det.abs() < 0.0000001 {
        return None;
    }
    let inv_det = 1.0 / det;
    let tvec = ro.sub(tri.v0);
    let u = tvec.dot(pvec) * inv_det;
    if !(0.0..=1.0).contains(&u) {
        return None;
    }
    let qvec = tvec.cross(e1);
    let v = rd.dot(qvec) * inv_det;
    if v < 0.0 || u + v > 1.0 {
        return None;
    }
    let t = e2.dot(qvec) * inv_det;
    if t < EPS || t >= max_t {
        return None;
    }

    let mut n = tri
        .n0
        .mul(1.0 - u - v)
        .add(tri.n1.mul(u))
        .add(tri.n2.mul(v))
        .normalize();
    if n.dot(rd) > 0.0 {
        n = n.mul(-1.0);
    }
    Some(Hit {
        t,
        n,
        emission: tri.emission,
    })
}

fn aabb_near(ro: Vec3, inv_rd: Vec3, bmin: Vec3, bmax: Vec3, max_t: f32) -> f32 {
    let t0 = Vec3::new(
        (bmin.x - ro.x) * inv_rd.x,
        (bmin.y - ro.y) * inv_rd.y,
        (bmin.z - ro.z) * inv_rd.z,
    );
    let t1 = Vec3::new(
        (bmax.x - ro.x) * inv_rd.x,
        (bmax.y - ro.y) * inv_rd.y,
        (bmax.z - ro.z) * inv_rd.z,
    );
    let lo = t0.min(t1);
    let hi = t0.max(t1);
    let near_t = lo.x.max(lo.y).max(lo.z);
    let far_t = hi.x.min(hi.y).min(hi.z);
    if far_t >= near_t.max(EPS) && near_t < max_t {
        near_t.max(0.0)
    } else {
        INF
    }
}

fn cosine_hemisphere(n: Vec3, seed: u32) -> Vec3 {
    let u = rand01(seed);
    let v = rand01(hash(seed));
    let r = u.sqrt();
    let a = 2.0 * std::f32::consts::PI * v;
    let t = tangent(n);
    let b = n.cross(t);
    t.mul(a.cos() * r)
        .add(b.mul(a.sin() * r))
        .add(n.mul((1.0 - u).max(0.0).sqrt()))
        .normalize()
}

fn tangent(n: Vec3) -> Vec3 {
    if n.y.abs() < 0.99 {
        n.cross(Vec3::new(0.0, 1.0, 0.0)).normalize()
    } else {
        n.cross(Vec3::new(1.0, 0.0, 0.0)).normalize()
    }
}

fn hash2(x: u32, y: u32) -> u32 {
    hash(x.wrapping_mul(1973) ^ y.wrapping_mul(9277) ^ 0x4f1bbcdd)
}

fn hash(mut x: u32) -> u32 {
    x ^= x >> 16;
    x = x.wrapping_mul(0x7feb352d);
    x ^= x >> 15;
    x = x.wrapping_mul(0x846ca68b);
    x ^ (x >> 16)
}

fn rand01(seed: u32) -> f32 {
    hash(seed) as f32 / 4_294_967_295.0
}

fn is_leaf(right_or_count: u32) -> bool {
    (right_or_count & 0x8000_0000) != 0
}

fn leaf_count(right_or_count: u32) -> usize {
    (right_or_count & 0x7fff_ffff) as usize
}

fn has_emission(emission: Vec3) -> bool {
    emission.dot(emission) > EMISSION_EPS2
}

fn score(counts: &Counts, triangle_weight: f64) -> f64 {
    counts.nodes as f64 + counts.triangles as f64 * triangle_weight
}

fn split_name(split: BvhSplit) -> &'static str {
    match split {
        BvhSplit::Median => "median",
        BvhSplit::BinnedSah => "binned_sah",
    }
}
