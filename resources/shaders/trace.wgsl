struct CameraUniform {
    camera_ro_time: vec4<f32>,
    camera_right: vec4<f32>,
    camera_up: vec4<f32>,
    camera_forward: vec4<f32>,
}

struct SceneBuffer {
    words: array<u32>,
}

@group(0) @binding(0) var out_image: texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(1) var<uniform> pc: CameraUniform;
@group(0) @binding(2) var<storage, read> scene: SceneBuffer;

const MAX_BOUNCES: i32 = 3;
const SAMPLES_PER_PIXEL: i32 = 3;
const SURFACE_EPSILON: f32 = 0.001;
const MAX_RAY_DISTANCE: f32 = 80.0;
const AXIS_NEVER: f32 = 1.0e20;
const PI: f32 = 3.14159265359;

const MAT_EMPTY: u32 = 0u;
const MAT_FLOOR: u32 = 1u;
const MAT_RED: u32 = 2u;
const MAT_GREEN: u32 = 3u;
const MAT_BLUE: u32 = 4u;
const MAT_LIGHT: u32 = 5u;

const LIGHT_LO: vec3<i32> = vec3<i32>(-3, 8, 1);
const LIGHT_HI: vec3<i32> = vec3<i32>(2, 9, 5);
const LIGHT_AREA: f32 = 20.0;

const CHUNK_SIZE: i32 = 4;
const SCENE_CHUNK_WORDS: u32 = 8u;
const SCENE_ACCEL_BOX_WORDS: u32 = 8u;
const SCENE_MATERIAL_BOX_WORDS: u32 = 8u;

struct Hit {
    t: f32,
    n: vec3<f32>,
    mat: u32,
}

struct VoxelHit {
    t: f32,
    n: vec3<f32>,
    voxel: vec3<i32>,
}

struct Camera {
    ro: vec3<f32>,
    right: vec3<f32>,
    up: vec3<f32>,
    forward: vec3<f32>,
}

fn scene_i32(index: u32) -> i32 {
    return bitcast<i32>(scene.words[index]);
}

fn chunk_count() -> u32 {
    return scene.words[0u];
}

fn accel_box_count() -> u32 {
    return scene.words[1u];
}

fn material_box_count() -> u32 {
    return scene.words[2u];
}

fn chunks_base() -> u32 {
    return scene.words[3u];
}

fn accel_boxes_base() -> u32 {
    return scene.words[4u];
}

fn material_boxes_base() -> u32 {
    return scene.words[5u];
}

fn chunk_entry_base(id: u32) -> u32 {
    return chunks_base() + id * SCENE_CHUNK_WORDS;
}

fn accel_box_base(id: u32) -> u32 {
    return accel_boxes_base() + id * SCENE_ACCEL_BOX_WORDS;
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
        (bitcast<u32>(pc.camera_ro_time.w) * 747796405u)
    );
}

fn albedo(m: u32) -> vec3<f32> {
    if m == MAT_RED {
        return vec3<f32>(0.8, 0.2, 0.2);
    }
    if m == MAT_GREEN {
        return vec3<f32>(0.2, 0.8, 0.2);
    }
    if m == MAT_BLUE {
        return vec3<f32>(0.2, 0.4, 0.9);
    }
    if m == MAT_FLOOR {
        return vec3<f32>(0.8);
    }
    return vec3<f32>(0.0);
}

fn emission(m: u32) -> vec3<f32> {
    if m == MAT_LIGHT {
        return vec3<f32>(8.0, 7.0, 6.0);
    }
    return vec3<f32>(0.0);
}

fn safe_inv(x: f32) -> f32 {
    if abs(x) < 0.000001 {
        return 1.0e20;
    }
    return 1.0 / x;
}

fn floor_div_4(v: i32) -> i32 {
    if v >= 0 {
        return v / CHUNK_SIZE;
    }
    return -((-v + CHUNK_SIZE - 1) / CHUNK_SIZE);
}

fn in_box(v: vec3<i32>, lo: vec3<i32>, hi: vec3<i32>) -> bool {
    return all(v >= lo) && all(v < hi);
}

fn mask_test(mask: vec2<u32>, local_voxel: vec3<i32>) -> bool {
    let bit = u32(local_voxel.x + local_voxel.y * CHUNK_SIZE + local_voxel.z * CHUNK_SIZE * CHUNK_SIZE);
    if bit < 32u {
        return (mask.x & (1u << bit)) != 0u;
    }
    return (mask.y & (1u << (bit - 32u))) != 0u;
}

fn voxel_occupied(v: vec3<i32>) -> bool {
    let chunk = vec3<i32>(floor_div_4(v.x), floor_div_4(v.y), floor_div_4(v.z));
    let local_voxel = v - chunk * CHUNK_SIZE;
    let count = chunk_count();

    for (var i = 0u; i < count; i = i + 1u) {
        let base = chunk_entry_base(i);
        let chunk_coord = vec3<i32>(
            scene_i32(base + 0u),
            scene_i32(base + 1u),
            scene_i32(base + 2u),
        );
        if all(chunk == chunk_coord) {
            return mask_test(vec2<u32>(scene.words[base + 3u], scene.words[base + 4u]), local_voxel);
        }
    }

    return false;
}

fn material_at_voxel(v: vec3<i32>) -> u32 {
    let count = material_box_count();
    for (var i = 0u; i < count; i = i + 1u) {
        let base = material_box_base(i);
        let lo = vec3<i32>(
            scene_i32(base + 0u),
            scene_i32(base + 1u),
            scene_i32(base + 2u),
        );
        let hi = vec3<i32>(
            scene_i32(base + 3u),
            scene_i32(base + 4u),
            scene_i32(base + 5u),
        );
        if in_box(v, lo, hi) {
            return scene.words[base + 6u];
        }
    }
    return MAT_EMPTY;
}

fn trace_accel_box(
    ro: vec3<f32>,
    rd: vec3<f32>,
    id: u32,
    max_distance: f32,
    out_hit: ptr<function, VoxelHit>,
) -> bool {
    let base = accel_box_base(id);
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
    let lo = vec3<f32>(lo_i);
    let hi = vec3<f32>(hi_i);
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

    var far_normal: vec3<f32>;
    if t1 == far_t.x {
        far_normal = vec3<f32>(select(-1.0, 1.0, rd.x > 0.0), 0.0, 0.0);
    } else if t1 == far_t.y {
        far_normal = vec3<f32>(0.0, select(-1.0, 1.0, rd.y > 0.0), 0.0);
    } else {
        far_normal = vec3<f32>(0.0, 0.0, select(-1.0, 1.0, rd.z > 0.0));
    }

    var t = t0;
    var n = near_normal;
    if t < SURFACE_EPSILON {
        t = t1;
        n = far_normal;
    }

    if t < SURFACE_EPSILON || t > max_distance {
        return false;
    }

    let voxel = vec3<i32>(floor(ro + rd * t - n * 0.0001));
    if !voxel_occupied(voxel) {
        return false;
    }

    *out_hit = VoxelHit(t, n, voxel);
    return true;
}

fn trace_voxels(
    ro: vec3<f32>,
    rd: vec3<f32>,
    max_distance: f32,
    out_hit: ptr<function, VoxelHit>,
) -> bool {
    var closest = max_distance;
    var found = false;
    let count = accel_box_count();

    for (var i = 0u; i < count; i = i + 1u) {
        var hit = VoxelHit(closest, vec3<f32>(0.0), vec3<i32>(0));
        if trace_accel_box(ro, rd, i, closest, &hit) {
            *out_hit = hit;
            closest = hit.t;
            found = true;
        }
    }

    return found;
}

fn trace_any_voxel(ro: vec3<f32>, rd: vec3<f32>, max_distance: f32) -> bool {
    let count = accel_box_count();
    for (var i = 0u; i < count; i = i + 1u) {
        var hit = VoxelHit(max_distance, vec3<f32>(0.0), vec3<i32>(0));
        if trace_accel_box(ro, rd, i, max_distance, &hit) {
            return true;
        }
    }
    return false;
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

    var voxel_hit = VoxelHit(closest, vec3<f32>(0.0), vec3<i32>(0));
    if trace_voxels(ro, rd, closest, &voxel_hit) {
        let mat = material_at_voxel(voxel_hit.voxel);
        if mat != MAT_EMPTY {
            *out_hit = Hit(voxel_hit.t, voxel_hit.n, mat);
            found = true;
        }
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
    let a = 2.51;
    let b = 0.03;
    let c = 2.43;
    let d = 0.59;
    let e = 0.14;
    return clamp((x * (a * x + vec3<f32>(b))) / (x * (c * x + vec3<f32>(d)) + vec3<f32>(e)), vec3<f32>(0.0), vec3<f32>(1.0));
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let px = vec2<i32>(i32(id.x), i32(id.y));
    let size_u = textureDimensions(out_image);
    let size = vec2<i32>(i32(size_u.x), i32(size_u.y));

    if any(px >= size) {
        return;
    }

    let cam = camera();
    var color = vec3<f32>(0.0);
    for (var i = 0; i < SAMPLES_PER_PIXEL; i = i + 1) {
        var rng = seed(px, i);
        let jitter = vec2<f32>(rand(&rng), rand(&rng)) - vec2<f32>(0.5);
        color += path_trace(cam.ro, camera_ray(cam, pixel_uv(px, size, jitter)), &rng);
    }

    color /= f32(SAMPLES_PER_PIXEL);
    color = pow(tonemap(color), vec3<f32>(1.0 / 2.2));

    textureStore(out_image, px, vec4<f32>(color, 1.0));
}
