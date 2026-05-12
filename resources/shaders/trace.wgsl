struct CameraUniform {
    camera_ro_time: vec4<f32>,
    camera_right: vec4<f32>,
    camera_up: vec4<f32>,
    camera_forward: vec4<f32>,
    render: vec4<u32>,
}

struct SceneBuffer {
    words: array<u32>,
}

@group(0) @binding(0) var color_image: texture_storage_2d<rgba16float, write>;
@group(0) @binding(1) var normal_image: texture_storage_2d<rgba16float, write>;
@group(0) @binding(2) var albedo_image: texture_storage_2d<rgba16float, write>;
@group(0) @binding(3) var depth_image: texture_storage_2d<rgba16float, write>;
@group(0) @binding(4) var<uniform> pc: CameraUniform;
@group(0) @binding(5) var<storage, read> scene: SceneBuffer;

const MAX_BOUNCES: i32 = 2;
const SURFACE_EPSILON: f32 = 0.001;
const MAX_RAY_DISTANCE: f32 = 80.0;
const AXIS_NEVER: f32 = 1.0e20;
const PI: f32 = 3.14159265359;

const MAT_EMPTY: u32 = 0u;
const MAT_FLOOR: u32 = 1u;
const MAT_GRASS: u32 = 2u;
const MAT_LEAF: u32 = 3u;
const MAT_WOOD: u32 = 4u;
const MAT_ROCK: u32 = 5u;
const MAT_SNOW: u32 = 6u;
const MAT_LIGHT: u32 = 7u;
const MAT_WATER: u32 = 8u;

const LIGHT_LO: vec3<i32> = vec3<i32>(-7, 16, -7);
const LIGHT_HI: vec3<i32> = vec3<i32>(8, 17, 8);
const LIGHT_AREA: f32 = 225.0;

const SCENE_BVH_NODE_WORDS: u32 = 16u;
const SCENE_MATERIAL_BOX_WORDS: u32 = 8u;
const BVH_STACK_SIZE: i32 = 64;
const RENDER_FLAG_GUIDES: u32 = 1u;

struct Hit {
    t: f32,
    n: vec3<f32>,
    mat: u32,
}

struct AabbHit {
    t0: f32,
    t1: f32,
    n0: vec3<f32>,
}

struct Camera {
    ro: vec3<f32>,
    right: vec3<f32>,
    up: vec3<f32>,
    forward: vec3<f32>,
}

struct Guides {
    normal: vec3<f32>,
    albedo: vec3<f32>,
    depth: f32,
}

fn scene_i32(index: u32) -> i32 {
    return bitcast<i32>(scene.words[index]);
}

fn bvh_node_count() -> u32 {
    return scene.words[0u];
}

fn bvh_root_ref() -> i32 {
    return scene_i32(1u);
}

fn material_box_count() -> u32 {
    return scene.words[2u];
}

fn bvh_nodes_base() -> u32 {
    return scene.words[3u];
}

fn material_boxes_base() -> u32 {
    return scene.words[4u];
}

fn bvh_node_base(id: u32) -> u32 {
    return bvh_nodes_base() + id * SCENE_BVH_NODE_WORDS;
}

fn material_box_base(id: u32) -> u32 {
    return material_boxes_base() + id * SCENE_MATERIAL_BOX_WORDS;
}

fn hash(x_in: u32) -> u32 {
    var x = x_in;
    x = x ^ (x >> 16u);
    x = x * 0x7feb352du;
    x = x ^ (x >> 15u);
    x = x * 0x846ca68bu;
    x = x ^ (x >> 16u);
    return x;
}

fn rand(s: ptr<function, u32>) -> f32 {
    *s = hash(*s);
    return f32(*s) / 4294967295.0;
}

fn seed(px: vec2<i32>, sample_index: i32) -> u32 {
    return hash(
        (u32(px.x) * 1973u) ^
        (u32(px.y) * 9277u) ^
        (u32(sample_index) * 26699u) ^
        (pc.render.y * 374761393u) ^
        (bitcast<u32>(pc.camera_ro_time.w) * 747796405u)
    );
}

fn albedo(m: u32) -> vec3<f32> {
    if m == MAT_GRASS {
        return vec3<f32>(0.32, 0.55, 0.22);
    }
    if m == MAT_LEAF {
        return vec3<f32>(0.10, 0.42, 0.16);
    }
    if m == MAT_WOOD {
        return vec3<f32>(0.42, 0.24, 0.10);
    }
    if m == MAT_ROCK {
        return vec3<f32>(0.42, 0.43, 0.40);
    }
    if m == MAT_SNOW {
        return vec3<f32>(0.86, 0.90, 0.92);
    }
    if m == MAT_WATER {
        return vec3<f32>(0.12, 0.35, 0.70);
    }
    if m == MAT_FLOOR {
        return vec3<f32>(0.58, 0.62, 0.52);
    }
    return vec3<f32>(0.0);
}

fn emission(m: u32) -> vec3<f32> {
    if m == MAT_LIGHT {
        return vec3<f32>(5.0, 4.7, 4.1);
    }
    return vec3<f32>(0.0);
}

fn safe_inv(x: f32) -> f32 {
    if abs(x) < 0.000001 {
        return 1.0e20;
    }
    return 1.0 / x;
}

fn trace_aabb(
    ro: vec3<f32>,
    rd: vec3<f32>,
    lo: vec3<f32>,
    hi: vec3<f32>,
    max_distance: f32,
    out_hit: ptr<function, AabbHit>,
) -> bool {
    let inv = vec3<f32>(safe_inv(rd.x), safe_inv(rd.y), safe_inv(rd.z));

    let a = (lo - ro) * inv;
    let b = (hi - ro) * inv;
    let near_t = min(a, b);
    let far_t = max(a, b);
    let t0 = max(max(near_t.x, near_t.y), near_t.z);
    let t1 = min(min(far_t.x, far_t.y), far_t.z);
    if t1 < max(t0, SURFACE_EPSILON) || t0 > max_distance {
        return false;
    }

    var near_normal: vec3<f32>;
    if t0 == near_t.x {
        near_normal = vec3<f32>(select(1.0, -1.0, rd.x > 0.0), 0.0, 0.0);
    } else if t0 == near_t.y {
        near_normal = vec3<f32>(0.0, select(1.0, -1.0, rd.y > 0.0), 0.0);
    } else {
        near_normal = vec3<f32>(0.0, 0.0, select(1.0, -1.0, rd.z > 0.0));
    }

    *out_hit = AabbHit(t0, t1, near_normal);
    return true;
}

fn intersects_aabb(
    ro: vec3<f32>,
    rd: vec3<f32>,
    lo: vec3<f32>,
    hi: vec3<f32>,
    max_distance: f32,
) -> bool {
    let inv = vec3<f32>(safe_inv(rd.x), safe_inv(rd.y), safe_inv(rd.z));

    let a = (lo - ro) * inv;
    let b = (hi - ro) * inv;
    let near_t = min(a, b);
    let far_t = max(a, b);
    let t0 = max(max(near_t.x, near_t.y), near_t.z);
    let t1 = min(min(far_t.x, far_t.y), far_t.z);
    return t1 >= max(t0, SURFACE_EPSILON) && t0 <= max_distance;
}

fn trace_material_box(
    ro: vec3<f32>,
    rd: vec3<f32>,
    id: u32,
    max_distance: f32,
    out_hit: ptr<function, Hit>,
) -> bool {
    let base = material_box_base(id);
    let lo_i = vec3<i32>(
        scene_i32(base + 0u),
        scene_i32(base + 1u),
        scene_i32(base + 2u),
    );
    let hi_i = vec3<i32>(
        scene_i32(base + 3u),
        scene_i32(base + 4u),
        scene_i32(base + 5u),
    );

    var box_hit = AabbHit(0.0, 0.0, vec3<f32>(0.0));
    if !trace_aabb(ro, rd, vec3<f32>(lo_i), vec3<f32>(hi_i), max_distance, &box_hit) {
        return false;
    }

    var t = box_hit.t0;
    var n = box_hit.n0;
    if t < SURFACE_EPSILON {
        t = box_hit.t1;
        let p = ro + rd * t;
        let eps = 0.0001;
        if abs(p.x - f32(lo_i.x)) < eps {
            n = vec3<f32>(-1.0, 0.0, 0.0);
        } else if abs(p.x - f32(hi_i.x)) < eps {
            n = vec3<f32>(1.0, 0.0, 0.0);
        } else if abs(p.y - f32(lo_i.y)) < eps {
            n = vec3<f32>(0.0, -1.0, 0.0);
        } else if abs(p.y - f32(hi_i.y)) < eps {
            n = vec3<f32>(0.0, 1.0, 0.0);
        } else if abs(p.z - f32(lo_i.z)) < eps {
            n = vec3<f32>(0.0, 0.0, -1.0);
        } else {
            n = vec3<f32>(0.0, 0.0, 1.0);
        }
    }

    if t < SURFACE_EPSILON || t > max_distance {
        return false;
    }

    *out_hit = Hit(t, n, scene.words[base + 6u]);
    return true;
}

fn intersects_material_box(ro: vec3<f32>, rd: vec3<f32>, id: u32, max_distance: f32) -> bool {
    let base = material_box_base(id);
    let lo_i = vec3<i32>(
        scene_i32(base + 0u),
        scene_i32(base + 1u),
        scene_i32(base + 2u),
    );
    let hi_i = vec3<i32>(
        scene_i32(base + 3u),
        scene_i32(base + 4u),
        scene_i32(base + 5u),
    );
    return intersects_aabb(ro, rd, vec3<f32>(lo_i), vec3<f32>(hi_i), max_distance);
}

fn trace_bvh_boxes(
    ro: vec3<f32>,
    rd: vec3<f32>,
    max_distance: f32,
    out_hit: ptr<function, Hit>,
) -> bool {
    let root = bvh_root_ref();
    if root <= 0 || bvh_node_count() == 0u {
        return false;
    }

    var closest = max_distance;
    var found = false;
    var stack: array<i32, 64>;
    var stack_size = 0;
    stack[stack_size] = root;
    stack_size += 1;

    for (var iter = 0; iter < BVH_STACK_SIZE; iter = iter + 1) {
        if stack_size <= 0 {
            break;
        }

        stack_size -= 1;
        let node_ref = stack[stack_size];
        if node_ref <= 0 {
            continue;
        }

        let node_id = u32(node_ref - 1);
        if node_id >= bvh_node_count() {
            continue;
        }

        let base = bvh_node_base(node_id);
        let lo = vec3<f32>(
            f32(scene_i32(base + 0u)),
            f32(scene_i32(base + 1u)),
            f32(scene_i32(base + 2u)),
        );
        let hi = vec3<f32>(
            f32(scene_i32(base + 3u)),
            f32(scene_i32(base + 4u)),
            f32(scene_i32(base + 5u)),
        );

        var node_hit = AabbHit(0.0, 0.0, vec3<f32>(0.0));
        if !trace_aabb(ro, rd, lo, hi, closest, &node_hit) {
            continue;
        }

        let left = scene_i32(base + 6u);
        let right = scene_i32(base + 7u);
        let first_box = scene.words[base + 8u];
        let box_count = scene.words[base + 9u];
        if box_count > 0u {
            for (var i = 0u; i < 4u; i = i + 1u) {
                if i >= box_count {
                    break;
                }
                var box_hit = Hit(closest, vec3<f32>(0.0), MAT_EMPTY);
                if trace_material_box(ro, rd, first_box + i, closest, &box_hit) {
                    *out_hit = box_hit;
                    closest = box_hit.t;
                    found = true;
                }
            }
        } else {
            if left > 0 && stack_size < BVH_STACK_SIZE {
                stack[stack_size] = left;
                stack_size += 1;
            }
            if right > 0 && stack_size < BVH_STACK_SIZE {
                stack[stack_size] = right;
                stack_size += 1;
            }
        }
    }

    return found;
}

fn trace_bvh_any_box(ro: vec3<f32>, rd: vec3<f32>, max_distance: f32) -> bool {
    let root = bvh_root_ref();
    if root <= 0 || bvh_node_count() == 0u {
        return false;
    }

    var stack: array<i32, 64>;
    var stack_size = 0;
    stack[stack_size] = root;
    stack_size += 1;

    for (var iter = 0; iter < BVH_STACK_SIZE; iter = iter + 1) {
        if stack_size <= 0 {
            break;
        }

        stack_size -= 1;
        let node_ref = stack[stack_size];
        if node_ref <= 0 {
            continue;
        }

        let node_id = u32(node_ref - 1);
        if node_id >= bvh_node_count() {
            continue;
        }

        let base = bvh_node_base(node_id);
        let lo = vec3<f32>(
            f32(scene_i32(base + 0u)),
            f32(scene_i32(base + 1u)),
            f32(scene_i32(base + 2u)),
        );
        let hi = vec3<f32>(
            f32(scene_i32(base + 3u)),
            f32(scene_i32(base + 4u)),
            f32(scene_i32(base + 5u)),
        );

        if !intersects_aabb(ro, rd, lo, hi, max_distance) {
            continue;
        }

        let left = scene_i32(base + 6u);
        let right = scene_i32(base + 7u);
        let first_box = scene.words[base + 8u];
        let box_count = scene.words[base + 9u];
        if box_count > 0u {
            for (var i = 0u; i < 4u; i = i + 1u) {
                if i >= box_count {
                    break;
                }
                if intersects_material_box(ro, rd, first_box + i, max_distance) {
                    return true;
                }
            }
        } else {
            if left > 0 && stack_size < BVH_STACK_SIZE {
                stack[stack_size] = left;
                stack_size += 1;
            }
            if right > 0 && stack_size < BVH_STACK_SIZE {
                stack[stack_size] = right;
                stack_size += 1;
            }
        }
    }

    return false;
}

fn trace_any_voxel(ro: vec3<f32>, rd: vec3<f32>, max_distance: f32) -> bool {
    return trace_bvh_any_box(ro, rd, max_distance);
}

fn trace(
    ro: vec3<f32>,
    rd: vec3<f32>,
    max_distance: f32,
    out_hit: ptr<function, Hit>,
) -> bool {
    var floor_t = AXIS_NEVER;
    if rd.y < 0.0 {
        floor_t = -ro.y / rd.y;
        if floor_t < SURFACE_EPSILON {
            floor_t = AXIS_NEVER;
        }
    }

    var found = false;
    var closest = max_distance;
    if floor_t <= max_distance {
        *out_hit = Hit(floor_t, vec3<f32>(0.0, 1.0, 0.0), MAT_FLOOR);
        closest = floor_t;
        found = true;
    }

    var bvh_hit = Hit(closest, vec3<f32>(0.0), MAT_EMPTY);
    if trace_bvh_boxes(ro, rd, closest, &bvh_hit) {
        *out_hit = bvh_hit;
        found = true;
    }

    return found;
}

fn sky(rd: vec3<f32>) -> vec3<f32> {
    let t = clamp(0.5 * (rd.y + 1.0), 0.0, 1.0);
    return mix(vec3<f32>(0.78, 0.84, 0.92), vec3<f32>(0.30, 0.40, 0.58), t);
}

fn tangent_for(n: vec3<f32>) -> vec3<f32> {
    var up = vec3<f32>(1.0, 0.0, 0.0);
    if abs(n.y) < 0.999 {
        up = vec3<f32>(0.0, 1.0, 0.0);
    }
    return normalize(cross(up, n));
}

fn cosine_hemisphere(n: vec3<f32>, rng: ptr<function, u32>) -> vec3<f32> {
    let r1 = rand(rng);
    let r2 = rand(rng);
    let a = 2.0 * PI * r1;
    let r = sqrt(r2);

    let t = tangent_for(n);
    let b = cross(n, t);
    let d = t * (cos(a) * r) +
        b * (sin(a) * r) +
        n * sqrt(max(0.0, 1.0 - r2));

    return normalize(d);
}

fn sample_light(rng: ptr<function, u32>) -> vec3<f32> {
    return vec3<f32>(
        mix(f32(LIGHT_LO.x), f32(LIGHT_HI.x), rand(rng)),
        f32(LIGHT_LO.y),
        mix(f32(LIGHT_LO.z), f32(LIGHT_HI.z), rand(rng)),
    );
}

fn sample_direct_light(p: vec3<f32>, n: vec3<f32>, mat: u32, rng: ptr<function, u32>) -> vec3<f32> {
    let light_point = sample_light(rng);
    let to_light = light_point - p;
    let distance_squared = dot(to_light, to_light);
    let distance_to_light = sqrt(distance_squared);
    let wi = to_light / distance_to_light;
    let surface_cos = max(dot(n, wi), 0.0);
    let light_cos = max(dot(vec3<f32>(0.0, -1.0, 0.0), -wi), 0.0);
    let w = surface_cos * light_cos;

    if w <= 0.0 {
        return vec3<f32>(0.0);
    }

    if trace_any_voxel(p, wi, distance_to_light - SURFACE_EPSILON) {
        return vec3<f32>(0.0);
    }

    return albedo(mat) / PI *
        emission(MAT_LIGHT) *
        w / max(distance_squared, 0.0001) *
        LIGHT_AREA;
}

fn path_trace(ro_in: vec3<f32>, rd_in: vec3<f32>, rng: ptr<function, u32>) -> vec3<f32> {
    var ro = ro_in;
    var rd = rd_in;
    var radiance = vec3<f32>(0.0);
    var throughput = vec3<f32>(1.0);

    for (var bounce = 0; bounce < MAX_BOUNCES; bounce = bounce + 1) {
        var hit = Hit(MAX_RAY_DISTANCE, vec3<f32>(0.0), MAT_EMPTY);
        if !trace(ro, rd, MAX_RAY_DISTANCE, &hit) {
            radiance += throughput * sky(rd);
            break;
        }

        let mat = hit.mat;
        let e = emission(mat);
        if max(e.r, max(e.g, e.b)) > 0.0 {
            if bounce == 0 {
                radiance += throughput * e;
            }
            break;
        }

        let p = ro + rd * hit.t;
        let n = hit.n;

        radiance += throughput * sample_direct_light(p + n * SURFACE_EPSILON, n, mat, rng);

        ro = p + n * SURFACE_EPSILON;
        rd = cosine_hemisphere(n, rng);
        throughput *= albedo(mat);
    }

    return radiance;
}

fn camera() -> Camera {
    return Camera(
        pc.camera_ro_time.xyz,
        pc.camera_right.xyz,
        pc.camera_up.xyz,
        pc.camera_forward.xyz,
    );
}

fn camera_ray(c: Camera, uv: vec2<f32>) -> vec3<f32> {
    return normalize(c.right * uv.x + c.up * uv.y + c.forward * pc.camera_forward.w);
}

fn pixel_uv(px: vec2<i32>, sz: vec2<i32>, jitter: vec2<f32>) -> vec2<f32> {
    var uv = (vec2<f32>(px) + vec2<f32>(0.5) + jitter) / vec2<f32>(sz) * 2.0 - vec2<f32>(1.0);
    uv.x *= f32(sz.x) / f32(sz.y);
    uv.y = -uv.y;
    return uv;
}

fn tonemap(x: vec3<f32>) -> vec3<f32> {
    return x / (x + vec3<f32>(1.0));
}

fn first_hit_guides(ro: vec3<f32>, rd: vec3<f32>) -> Guides {
    var hit = Hit(MAX_RAY_DISTANCE, vec3<f32>(0.0), MAT_EMPTY);
    if !trace(ro, rd, MAX_RAY_DISTANCE, &hit) {
        return Guides(vec3<f32>(0.5), vec3<f32>(0.0), 1.0);
    }

    let encoded_normal = hit.n * 0.5 + vec3<f32>(0.5);
    return Guides(encoded_normal, albedo(hit.mat), clamp(hit.t / MAX_RAY_DISTANCE, 0.0, 1.0));
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let px = vec2<i32>(i32(id.x), i32(id.y));
    let size_u = textureDimensions(color_image);
    let size = vec2<i32>(i32(size_u.x), i32(size_u.y));

    if any(px >= size) {
        return;
    }

    let cam = camera();
    let sample_count = max(i32(pc.render.x), 1);
    var color = vec3<f32>(0.0);
    let write_guides = (pc.render.z & RENDER_FLAG_GUIDES) != 0u;
    var guides = Guides(vec3<f32>(0.5), vec3<f32>(0.0), 1.0);
    if write_guides {
        let center_ray = camera_ray(cam, pixel_uv(px, size, vec2<f32>(0.0)));
        guides = first_hit_guides(cam.ro, center_ray);
    }

    for (var i = 0; i < sample_count; i = i + 1) {
        var rng = seed(px, i);
        let jitter = vec2<f32>(rand(&rng), rand(&rng)) - vec2<f32>(0.5);
        color += path_trace(cam.ro, camera_ray(cam, pixel_uv(px, size, jitter)), &rng);
    }

    color /= f32(sample_count);
    color = tonemap(color);
    textureStore(color_image, px, vec4<f32>(color, 1.0));
    if write_guides {
        textureStore(normal_image, px, vec4<f32>(guides.normal, 1.0));
        textureStore(albedo_image, px, vec4<f32>(guides.albedo, 1.0));
        textureStore(depth_image, px, vec4<f32>(guides.depth, 0.0, 0.0, 1.0));
    }
}
