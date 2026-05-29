struct CameraUniform {
    camera_ro_time: vec4<f32>,
    camera_right: vec4<f32>,
    camera_up: vec4<f32>,
    camera_forward: vec4<f32>,
    render: vec4<u32>,
}

struct Projection {
    px: vec2<f32>,
    depth: f32,
    valid: bool,
}

struct ColorBounds {
    lo: vec3<f32>,
    hi: vec3<f32>,
    luma_span: f32,
}

struct HistoryCandidate {
    color: vec3<f32>,
    count: f32,
    weight: f32,
}

@group(0) @binding(0) var current_color: texture_2d<f32>;
@group(0) @binding(2) var current_albedo: texture_2d<f32>;
@group(0) @binding(3) var current_depth: texture_2d<f32>;
@group(0) @binding(4) var previous_history_color: texture_2d<f32>;
@group(0) @binding(6) var previous_history_albedo_depth: texture_2d<f32>;
@group(0) @binding(8) var<uniform> pc: CameraUniform;
@group(0) @binding(9) var<uniform> previous_pc: CameraUniform;
@group(0) @binding(20) var final_image: texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(21) var next_history_color: texture_storage_2d<rgba16float, write>;
@group(0) @binding(23) var next_history_albedo_depth: texture_storage_2d<rgba16float, write>;

const MAX_RAY_DISTANCE: f32 = 80.0;
const MAX_HISTORY: f32 = 8.0;
const MAX_HISTORY_WEIGHT: f32 = 0.90;
const MOTION_DECAY: f32 = 0.06;
const MOTION_WEIGHT_DECAY: f32 = 0.04;
const MIN_MOTION_HISTORY_CAP: f32 = 0.45;
const LOCAL_CONTRAST_DECAY: f32 = 2.0;
const MIN_CONTRAST_HISTORY_CAP: f32 = 0.70;
const VALIDATION_SHARPNESS: f32 = 0.75;
const DEPTH_THRESHOLD: f32 = 0.01;
const ALBEDO_THRESHOLD: f32 = 0.15;
const SKY_DEPTH: f32 = 0.999;
const CLAMP_MARGIN: f32 = 0.04;

fn clamp_px(px: vec2<i32>, size: vec2<i32>) -> vec2<i32> {
    return clamp(px, vec2<i32>(0), size - vec2<i32>(1));
}

fn safe_normalize(v: vec3<f32>) -> vec3<f32> {
    let len = length(v);
    if len < 0.000001 {
        return vec3<f32>(0.0);
    }
    return v / len;
}

fn saturate(x: f32) -> f32 {
    return clamp(x, 0.0, 1.0);
}

fn fast_decay(x: f32) -> f32 {
    let y = max(x, 0.0);
    return 1.0 / (1.0 + y + 0.48 * y * y);
}

fn decode_normal(encoded: vec3<f32>) -> vec3<f32> {
    return encoded * 2.0 - vec3<f32>(1.0);
}

fn pixel_uv(px: vec2<i32>, size: vec2<i32>) -> vec2<f32> {
    var uv = (vec2<f32>(px) + vec2<f32>(0.5)) / vec2<f32>(size) * 2.0 - vec2<f32>(1.0);
    uv.x *= f32(size.x) / f32(size.y);
    uv.y = -uv.y;
    return uv;
}

fn camera_ray(c: CameraUniform, uv: vec2<f32>) -> vec3<f32> {
    return safe_normalize(c.camera_right.xyz * uv.x + c.camera_up.xyz * uv.y + c.camera_forward.xyz * c.camera_forward.w);
}

fn project_point(point: vec3<f32>, c: CameraUniform, size: vec2<i32>) -> Projection {
    let v = point - c.camera_ro_time.xyz;
    let distance = length(v);
    let d = v / max(distance, 0.000001);
    let denom = dot(d, c.camera_forward.xyz);
    let aspect = f32(size.x) / f32(size.y);
    let uv_x = c.camera_forward.w * dot(d, c.camera_right.xyz) / max(denom, 0.000001);
    let uv_y = c.camera_forward.w * dot(d, c.camera_up.xyz) / max(denom, 0.000001);
    let x = ((uv_x / aspect) + 1.0) * 0.5 * f32(size.x) - 0.5;
    let y = ((-uv_y) + 1.0) * 0.5 * f32(size.y) - 0.5;
    let valid =
        denom > 0.00001 &&
        x >= 0.0 &&
        y >= 0.0 &&
        x <= f32(size.x - 1) &&
        y <= f32(size.y - 1);
    return Projection(vec2<f32>(x, y), distance / MAX_RAY_DISTANCE, valid);
}

fn local_bounds(px: vec2<i32>, size: vec2<i32>) -> ColorBounds {
    var lo = vec3<f32>(1.0);
    var hi = vec3<f32>(0.0);
    var luma_lo = 1.0;
    var luma_hi = 0.0;
    var found = false;

    for (var tap = 0u; tap < 5u; tap = tap + 1u) {
        var offset = vec2<i32>(0, 0);
        if tap == 1u {
            offset = vec2<i32>(1, 0);
        } else if tap == 2u {
            offset = vec2<i32>(-1, 0);
        } else if tap == 3u {
            offset = vec2<i32>(0, 1);
        } else if tap == 4u {
            offset = vec2<i32>(0, -1);
        }

        let q = clamp_px(px + offset, size);
        let sample = textureLoad(current_color, q, 0);
        if sample.a > 0.0 {
            lo = min(lo, sample.rgb);
            hi = max(hi, sample.rgb);
            let luma = dot(sample.rgb, vec3<f32>(0.2126, 0.7152, 0.0722));
            luma_lo = min(luma_lo, luma);
            luma_hi = max(luma_hi, luma);
            found = true;
        }
    }

    if !found {
        let center = textureLoad(current_color, px, 0).rgb;
        lo = center;
        hi = center;
        let luma = dot(center, vec3<f32>(0.2126, 0.7152, 0.0722));
        luma_lo = luma;
        luma_hi = luma;
    }

    return ColorBounds(
        max(lo - vec3<f32>(CLAMP_MARGIN), vec3<f32>(0.0)),
        min(hi + vec3<f32>(CLAMP_MARGIN), vec3<f32>(1.0)),
        luma_hi - luma_lo
    );
}

fn adaptive_history_cap(motion: f32, local_span: f32) -> f32 {
    let motion_cap = mix(
        MIN_MOTION_HISTORY_CAP,
        MAX_HISTORY_WEIGHT,
        fast_decay(MOTION_WEIGHT_DECAY * motion)
    );
    let contrast_cap = mix(
        MIN_CONTRAST_HISTORY_CAP,
        1.0,
        fast_decay(LOCAL_CONTRAST_DECAY * local_span)
    );
    return min(MAX_HISTORY_WEIGHT, motion_cap * contrast_cap);
}

fn sample_history_candidate(
    sample_px: vec2<i32>,
    bilinear_weight: f32,
    projected_depth: f32,
    current_albedo_sample: vec3<f32>,
) -> HistoryCandidate {
    if bilinear_weight <= 0.000001 {
        return HistoryCandidate(vec3<f32>(0.0), 0.0, 0.0);
    }

    let previous_history = textureLoad(previous_history_color, sample_px, 0);
    let previous_count = previous_history.a;
    if previous_count <= 0.0 {
        return HistoryCandidate(vec3<f32>(0.0), 0.0, 0.0);
    }

    let previous_albedo_depth_sample = textureLoad(previous_history_albedo_depth, sample_px, 0);
    let previous_depth_sample = previous_albedo_depth_sample.a;
    if previous_depth_sample >= SKY_DEPTH {
        return HistoryCandidate(vec3<f32>(0.0), 0.0, 0.0);
    }

    let depth_delta = abs(previous_depth_sample - projected_depth);
    let previous_albedo_sample = previous_albedo_depth_sample.rgb;
    let albedo_delta_rgb = abs(previous_albedo_sample - current_albedo_sample);
    let albedo_delta = (albedo_delta_rgb.x + albedo_delta_rgb.y + albedo_delta_rgb.z) / 3.0;

    if depth_delta >= DEPTH_THRESHOLD || albedo_delta >= ALBEDO_THRESHOLD {
        return HistoryCandidate(vec3<f32>(0.0), 0.0, 0.0);
    }

    let depth_score = depth_delta / DEPTH_THRESHOLD;
    let albedo_score = albedo_delta / ALBEDO_THRESHOLD;
    let guide_linear = saturate(1.0 - (depth_score + albedo_score) * VALIDATION_SHARPNESS * 0.55);
    let guide_weight = guide_linear * guide_linear;
    return HistoryCandidate(previous_history.rgb, previous_count, bilinear_weight * guide_weight);
}

fn display_store(px: vec2<i32>, color: vec3<f32>) {
    let display = pow(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), vec3<f32>(1.0 / 2.2));
    textureStore(final_image, px, vec4<f32>(display, 1.0));
}

@compute @workgroup_size(8, 8, 1)
fn copy_main(@builtin(global_invocation_id) id: vec3<u32>) {
    let px = vec2<i32>(i32(id.x), i32(id.y));
    let size_u = textureDimensions(current_color);
    let size = vec2<i32>(i32(size_u.x), i32(size_u.y));
    if any(px >= size) {
        return;
    }

    display_store(px, textureLoad(current_color, px, 0).rgb);
}

@compute @workgroup_size(8, 8, 1)
fn filter_main(@builtin(global_invocation_id) id: vec3<u32>) {
    let px = vec2<i32>(i32(id.x), i32(id.y));
    let size_u = textureDimensions(current_color);
    let size = vec2<i32>(i32(size_u.x), i32(size_u.y));
    if any(px >= size) {
        return;
    }

    let current_sample = textureLoad(current_color, px, 0);
    let current_albedo_sample = textureLoad(current_albedo, px, 0).rgb;
    let current_depth_sample = textureLoad(current_depth, px, 0).x;
    let current_covered = current_sample.a > 0.0;

    var out_color = current_sample.rgb;
    var next_count = 0.0;
    if current_covered {
        next_count = 1.0;
    }

    var has_history = false;
    var previous_color_sample = vec3<f32>(0.0);
    var previous_count = 0.0;
    var confidence = 0.0;
    var motion = 0.0;

    if pc.render.y != 0u && current_depth_sample < SKY_DEPTH {
        let ray = camera_ray(pc, pixel_uv(px, size));
        let point = pc.camera_ro_time.xyz + ray * (current_depth_sample * MAX_RAY_DISTANCE);
        let projected = project_point(point, previous_pc, size);
        if projected.valid {
            let base_f = floor(projected.px);
            let base_px = vec2<i32>(i32(base_f.x), i32(base_f.y));
            let frac_px = clamp(projected.px - base_f, vec2<f32>(0.0), vec2<f32>(1.0));

            let candidate_00 = sample_history_candidate(
                clamp_px(base_px, size),
                (1.0 - frac_px.x) * (1.0 - frac_px.y),
                projected.depth,
                current_albedo_sample
            );
            let candidate_10 = sample_history_candidate(
                clamp_px(base_px + vec2<i32>(1, 0), size),
                frac_px.x * (1.0 - frac_px.y),
                projected.depth,
                current_albedo_sample
            );
            let candidate_01 = sample_history_candidate(
                clamp_px(base_px + vec2<i32>(0, 1), size),
                (1.0 - frac_px.x) * frac_px.y,
                projected.depth,
                current_albedo_sample
            );
            let candidate_11 = sample_history_candidate(
                clamp_px(base_px + vec2<i32>(1, 1), size),
                frac_px.x * frac_px.y,
                projected.depth,
                current_albedo_sample
            );

            let candidate_weight =
                candidate_00.weight +
                candidate_10.weight +
                candidate_01.weight +
                candidate_11.weight;
            if candidate_weight > 0.000001 {
                previous_color_sample = (
                    candidate_00.color * candidate_00.weight +
                    candidate_10.color * candidate_10.weight +
                    candidate_01.color * candidate_01.weight +
                    candidate_11.color * candidate_11.weight
                ) / candidate_weight;
                previous_count = (
                    candidate_00.count * candidate_00.weight +
                    candidate_10.count * candidate_10.weight +
                    candidate_01.count * candidate_01.weight +
                    candidate_11.count * candidate_11.weight
                ) / candidate_weight;
                motion = length(projected.px - vec2<f32>(px));
                confidence = fast_decay(MOTION_DECAY * motion) * min(candidate_weight, 1.0);
                has_history = confidence > 0.000001 && previous_count > 0.0;
            }
        }
    }

    if current_covered {
        if has_history {
            let bounds = local_bounds(px, size);
            let clamped_history = clamp(previous_color_sample, bounds.lo, bounds.hi);
            let capped_count = min(previous_count, MAX_HISTORY);
            let history_cap = adaptive_history_cap(motion, bounds.luma_span);
            let history_weight = min(
                confidence * capped_count / (capped_count + 1.0),
                history_cap
            );
            out_color = mix(current_sample.rgb, clamped_history, history_weight);
            next_count = clamp(
                1.0 + confidence * min(previous_count, MAX_HISTORY - 1.0),
                1.0,
                MAX_HISTORY
            );
        } else {
            out_color = current_sample.rgb;
            next_count = 1.0;
        }
    } else if has_history {
        out_color = previous_color_sample;
        next_count = clamp(confidence * min(previous_count, MAX_HISTORY), 0.0, MAX_HISTORY);
    }

    out_color = clamp(out_color, vec3<f32>(0.0), vec3<f32>(1.0));
    display_store(px, out_color);
    textureStore(next_history_color, px, vec4<f32>(out_color, next_count));
    textureStore(next_history_albedo_depth, px, vec4<f32>(current_albedo_sample, current_depth_sample));
}
