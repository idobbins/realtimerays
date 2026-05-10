struct Weights {
    values: array<f32>,
}

@group(0) @binding(0) var noisy_image: texture_2d<f32>;
@group(0) @binding(1) var normal_image: texture_2d<f32>;
@group(0) @binding(2) var albedo_image: texture_2d<f32>;
@group(0) @binding(3) var depth_image: texture_2d<f32>;
@group(0) @binding(6) var<storage, read> weights: Weights;
@group(0) @binding(20) var final_image: texture_storage_2d<rgba8unorm, write>;

const MAX_FILTER_RADIUS: i32 = 2;
const MAX_FILTER_SIZE: u32 = 5u;
const MAX_FILTER_TAPS: u32 = MAX_FILTER_SIZE * MAX_FILTER_SIZE;

const RADIUS_OFFSET: u32 = 0u;
const BLEND_OFFSET: u32 = 1u;
const COLOR_PENALTY_OFFSET: u32 = 2u;
const NORMAL_PENALTY_OFFSET: u32 = 3u;
const ALBEDO_PENALTY_OFFSET: u32 = 4u;
const DEPTH_PENALTY_OFFSET: u32 = 5u;
const SPATIAL_OFFSET: u32 = 6u;
const DISABLED_PENALTY: f32 = -10.0;

fn clamp_px(px: vec2<i32>, size: vec2<i32>) -> vec2<i32> {
    return clamp(px, vec2<i32>(0), size - vec2<i32>(1));
}

fn sigmoid(x: f32) -> f32 {
    return 1.0 / (1.0 + exp(-x));
}

fn softplus(x: f32) -> f32 {
    return log(1.0 + exp(clamp(x, -20.0, 20.0)));
}

fn sqr_len3(v: vec3<f32>) -> f32 {
    return dot(v, v);
}

fn tap_index(ox: i32, oy: i32) -> u32 {
    let x = u32(ox + MAX_FILTER_RADIUS);
    let y = u32(oy + MAX_FILTER_RADIUS);
    return y * MAX_FILTER_SIZE + x;
}

fn display_store(px: vec2<i32>, color: vec3<f32>) {
    let display = pow(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), vec3<f32>(1.0 / 2.2));
    textureStore(final_image, px, vec4<f32>(display, 1.0));
}

@compute @workgroup_size(8, 8, 1)
fn copy_main(@builtin(global_invocation_id) id: vec3<u32>) {
    let px = vec2<i32>(i32(id.x), i32(id.y));
    let size_u = textureDimensions(noisy_image);
    let size = vec2<i32>(i32(size_u.x), i32(size_u.y));
    if any(px >= size) {
        return;
    }

    display_store(px, textureLoad(noisy_image, px, 0).rgb);
}

@compute @workgroup_size(8, 8, 1)
fn filter_main(@builtin(global_invocation_id) id: vec3<u32>) {
    let px = vec2<i32>(i32(id.x), i32(id.y));
    let size_u = textureDimensions(noisy_image);
    let size = vec2<i32>(i32(size_u.x), i32(size_u.y));
    if any(px >= size) {
        return;
    }

    let center = clamp_px(px, size);
    let center_color = textureLoad(noisy_image, center, 0).rgb;
    let radius = i32(clamp(round(weights.values[RADIUS_OFFSET]), 1.0, f32(MAX_FILTER_RADIUS)));

    let color_raw = weights.values[COLOR_PENALTY_OFFSET];
    let normal_raw = weights.values[NORMAL_PENALTY_OFFSET];
    let albedo_raw = weights.values[ALBEDO_PENALTY_OFFSET];
    let depth_raw = weights.values[DEPTH_PENALTY_OFFSET];
    let use_color = color_raw > DISABLED_PENALTY;
    let use_normal = normal_raw > DISABLED_PENALTY;
    let use_albedo = albedo_raw > DISABLED_PENALTY;
    let use_depth = depth_raw > DISABLED_PENALTY;

    let color_penalty = softplus(color_raw);
    let normal_penalty = softplus(normal_raw);
    let albedo_penalty = softplus(albedo_raw);
    let depth_penalty = softplus(depth_raw);
    let blend = sigmoid(weights.values[BLEND_OFFSET]);

    var center_normal = vec3<f32>(0.0);
    var center_albedo = vec3<f32>(0.0);
    var center_depth = 0.0;
    if use_normal {
        center_normal = textureLoad(normal_image, center, 0).rgb;
    }
    if use_albedo {
        center_albedo = textureLoad(albedo_image, center, 0).rgb;
    }
    if use_depth {
        center_depth = textureLoad(depth_image, center, 0).x;
    }

    var sum_color = vec3<f32>(0.0);
    var sum_weight = 0.0;
    for (var oy = -MAX_FILTER_RADIUS; oy <= MAX_FILTER_RADIUS; oy = oy + 1) {
        for (var ox = -MAX_FILTER_RADIUS; ox <= MAX_FILTER_RADIUS; ox = ox + 1) {
            if abs(ox) > radius || abs(oy) > radius {
                continue;
            }

            let q = clamp_px(px + vec2<i32>(ox, oy), size);
            let q_color = textureLoad(noisy_image, q, 0).rgb;
            var log_weight = weights.values[SPATIAL_OFFSET + tap_index(ox, oy)];

            if use_color {
                log_weight -= color_penalty * sqr_len3(q_color - center_color);
            }
            if use_normal {
                let q_normal = textureLoad(normal_image, q, 0).rgb;
                log_weight -= normal_penalty * sqr_len3(q_normal - center_normal);
            }
            if use_albedo {
                let q_albedo = textureLoad(albedo_image, q, 0).rgb;
                log_weight -= albedo_penalty * sqr_len3(q_albedo - center_albedo);
            }
            if use_depth {
                let q_depth = textureLoad(depth_image, q, 0).x;
                let depth_diff = q_depth - center_depth;
                log_weight -= depth_penalty * depth_diff * depth_diff;
            }

            let w = exp(clamp(log_weight, -20.0, 20.0));
            sum_color += q_color * w;
            sum_weight += w;
        }
    }

    let filtered = sum_color / max(sum_weight, 0.000001);
    display_store(px, mix(center_color, filtered, blend));
}
