struct Weights {
    values: array<f32>,
}

@group(0) @binding(0) var noisy_image: texture_2d<f32>;
@group(0) @binding(1) var normal_image: texture_2d<f32>;
@group(0) @binding(2) var albedo_image: texture_2d<f32>;
@group(0) @binding(3) var depth_image: texture_2d<f32>;
@group(0) @binding(6) var<storage, read> weights: Weights;
@group(0) @binding(20) var final_image: texture_storage_2d<rgba8unorm, write>;

const HIDDEN_CHANNELS: u32 = 8u;
const INPUT_CHANNELS: u32 = 10u;
const KERNEL_SIZE: u32 = 3u;
const CONV0_WEIGHT_OFFSET: u32 = 0u;
const CONV0_BIAS_OFFSET: u32 = 720u;
const CONV1_WEIGHT_OFFSET: u32 = 728u;
const CONV1_BIAS_OFFSET: u32 = 736u;
const MAX_LUMA_DELTA: f32 = 0.12;

fn clamp_px(px: vec2<i32>, size: vec2<i32>) -> vec2<i32> {
    return clamp(px, vec2<i32>(0), size - vec2<i32>(1));
}

fn luminance(c: vec3<f32>) -> f32 {
    return dot(c, vec3<f32>(0.2126, 0.7152, 0.0722));
}

fn tanh_approx(x: f32) -> f32 {
    let y = clamp(x, -10.0, 10.0);
    return 2.0 / (1.0 + exp(-2.0 * y)) - 1.0;
}

fn display_store(px: vec2<i32>, color: vec3<f32>) {
    let display = pow(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), vec3<f32>(1.0 / 2.2));
    textureStore(final_image, px, vec4<f32>(display, 1.0));
}

fn conv0_weight_index(hidden: u32, channel: u32, ky: u32, kx: u32) -> u32 {
    return CONV0_WEIGHT_OFFSET
        + (((hidden * INPUT_CHANNELS + channel) * KERNEL_SIZE + ky) * KERNEL_SIZE + kx);
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
    let center_luma = luminance(center_color);
    let center_normal = textureLoad(normal_image, center, 0).rgb;
    let center_albedo = textureLoad(albedo_image, center, 0).rgb;
    let center_depth = textureLoad(depth_image, center, 0).x;

    var hidden: array<f32, 8>;
    for (var h = 0u; h < HIDDEN_CHANNELS; h = h + 1u) {
        hidden[h] = weights.values[CONV0_BIAS_OFFSET + h];
    }

    for (var oy = -1; oy <= 1; oy = oy + 1) {
        for (var ox = -1; ox <= 1; ox = ox + 1) {
            let q = clamp_px(px + vec2<i32>(ox, oy), size);
            let q_color = textureLoad(noisy_image, q, 0).rgb;
            let q_normal = textureLoad(normal_image, q, 0).rgb;
            let q_albedo = textureLoad(albedo_image, q, 0).rgb;
            let q_depth = textureLoad(depth_image, q, 0).x;
            let q_luma = luminance(q_color);
            let ky = u32(oy + 1);
            let kx = u32(ox + 1);

            var channels: array<f32, 10>;
            channels[0] = q_luma;
            channels[1] = q_luma - center_luma;
            channels[2] = q_normal.x - center_normal.x;
            channels[3] = q_normal.y - center_normal.y;
            channels[4] = q_normal.z - center_normal.z;
            channels[5] = q_albedo.x - center_albedo.x;
            channels[6] = q_albedo.y - center_albedo.y;
            channels[7] = q_albedo.z - center_albedo.z;
            channels[8] = q_depth - center_depth;
            channels[9] = center_luma;

            for (var h = 0u; h < HIDDEN_CHANNELS; h = h + 1u) {
                for (var c = 0u; c < INPUT_CHANNELS; c = c + 1u) {
                    hidden[h] = hidden[h] + channels[c] * weights.values[conv0_weight_index(h, c, ky, kx)];
                }
            }
        }
    }

    var raw_delta_luma = weights.values[CONV1_BIAS_OFFSET];
    for (var h = 0u; h < HIDDEN_CHANNELS; h = h + 1u) {
        let activation = max(hidden[h], 0.0);
        raw_delta_luma = raw_delta_luma + activation * weights.values[CONV1_WEIGHT_OFFSET + h];
    }

    let delta_luma = MAX_LUMA_DELTA * tanh_approx(raw_delta_luma);
    let out_luma = max(center_luma + delta_luma, 0.0);
    let chroma = center_color / max(center_luma, 0.0001);
    display_store(px, chroma * out_luma);
}
