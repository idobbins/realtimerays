struct Weights {
    values: array<f32>,
}

@group(0) @binding(0) var noisy_image: texture_2d<f32>;
@group(0) @binding(6) var<storage, read> weights: Weights;
@group(0) @binding(20) var final_image: texture_storage_2d<rgba8unorm, write>;

const HIDDEN_CHANNELS: u32 = 8u;
const INPUT_CHANNELS: u32 = 3u;
const CONV0_WEIGHT_OFFSET: u32 = 0u;
const MAX_RGB_DELTA: f32 = 0.22;
const CHROMA_EPSILON: f32 = 0.0001;
const CHROMA_FALLBACK_LUMA: f32 = 0.01;
override RGB_CNN_MODE: u32 = 0u;
override RGB_CNN_INPUT_CHANNELS: u32 = 3u;
override RGB_CNN_OUTPUT_CHANNELS: u32 = 3u;
override RGB_CNN_LAYOUT: u32 = 0u;
override RGB_CNN_TAP_COUNT: u32 = 9u;
override RGB_CNN_KERNEL_RADIUS: u32 = 1u;
override RGB_CNN_DILATION: u32 = 1u;

fn clamp_px(px: vec2<i32>, size: vec2<i32>) -> vec2<i32> {
    return clamp(px, vec2<i32>(0), size - vec2<i32>(1));
}

fn tanh_approx(x: f32) -> f32 {
    let y = clamp(x, -10.0, 10.0);
    return 2.0 / (1.0 + exp(-2.0 * y)) - 1.0;
}

fn sigmoid_approx(x: f32) -> f32 {
    let y = clamp(x, -10.0, 10.0);
    return 1.0 / (1.0 + exp(-y));
}

fn display_store(px: vec2<i32>, color: vec3<f32>) {
    let display = pow(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), vec3<f32>(1.0 / 2.2));
    textureStore(final_image, px, vec4<f32>(display, 1.0));
}

fn luminance(color: vec3<f32>) -> f32 {
    return dot(color, vec3<f32>(0.2126, 0.7152, 0.0722));
}

fn rgb_cnn_conv0_bias_offset() -> u32 {
    return HIDDEN_CHANNELS * RGB_CNN_INPUT_CHANNELS * RGB_CNN_TAP_COUNT;
}

fn rgb_cnn_conv1_weight_offset() -> u32 {
    return rgb_cnn_conv0_bias_offset() + HIDDEN_CHANNELS;
}

fn rgb_cnn_conv1_bias_offset() -> u32 {
    return rgb_cnn_conv1_weight_offset() + RGB_CNN_OUTPUT_CHANNELS * HIDDEN_CHANNELS;
}

fn sparse_wide_offset(tap: u32) -> vec2<i32> {
    if tap < 25u {
        let x = i32(tap % 5u) - 2;
        let y = i32(tap / 5u) - 2;
        return vec2<i32>(x, y);
    }

    let ring_tap = tap - 25u;
    let ring = ring_tap / 8u;
    let pos = ring_tap % 8u;
    var radius = 4;
    if ring == 1u {
        radius = 8;
    }
    if ring == 2u {
        radius = 12;
    }

    if pos == 0u {
        return vec2<i32>(radius, 0);
    }
    if pos == 1u {
        return vec2<i32>(-radius, 0);
    }
    if pos == 2u {
        return vec2<i32>(0, radius);
    }
    if pos == 3u {
        return vec2<i32>(0, -radius);
    }
    if pos == 4u {
        return vec2<i32>(radius, radius);
    }
    if pos == 5u {
        return vec2<i32>(-radius, radius);
    }
    if pos == 6u {
        return vec2<i32>(radius, -radius);
    }
    return vec2<i32>(-radius, -radius);
}

fn dilated_offset(tap: u32, kernel_size: u32) -> vec2<i32> {
    let radius = i32(kernel_size / 2u);
    let x = (i32(tap % kernel_size) - radius) * i32(RGB_CNN_DILATION);
    let y = (i32(tap / kernel_size) - radius) * i32(RGB_CNN_DILATION);
    return vec2<i32>(x, y);
}

fn axis17_offset(tap: u32) -> vec2<i32> {
    if tap < 17u {
        return vec2<i32>(i32(tap) - 8, 0);
    }
    return vec2<i32>(0, i32(tap - 17u) - 8);
}

fn rgb_cnn_tap_offset(tap: u32) -> vec2<i32> {
    if RGB_CNN_LAYOUT == 1u {
        return sparse_wide_offset(tap);
    }
    if RGB_CNN_LAYOUT == 2u {
        return dilated_offset(tap, 3u);
    }
    if RGB_CNN_LAYOUT == 3u {
        return dilated_offset(tap, 5u);
    }
    if RGB_CNN_LAYOUT == 4u {
        return axis17_offset(tap);
    }

    let kernel_size = RGB_CNN_KERNEL_RADIUS * 2u + 1u;
    let x = i32(tap % kernel_size) - i32(RGB_CNN_KERNEL_RADIUS);
    let y = i32(tap / kernel_size) - i32(RGB_CNN_KERNEL_RADIUS);
    return vec2<i32>(x, y);
}

fn conv0_weight_index(hidden: u32, channel: u32, tap: u32) -> u32 {
    return CONV0_WEIGHT_OFFSET + ((hidden * RGB_CNN_INPUT_CHANNELS + channel) * RGB_CNN_TAP_COUNT + tap);
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
    let conv0_bias_base = rgb_cnn_conv0_bias_offset();

    var hidden: array<f32, 8>;
    for (var h = 0u; h < HIDDEN_CHANNELS; h = h + 1u) {
        hidden[h] = weights.values[conv0_bias_base + h];
    }

    for (var tap = 0u; tap < RGB_CNN_TAP_COUNT; tap = tap + 1u) {
        let offset = rgb_cnn_tap_offset(tap);
        let q = clamp_px(px + offset, size);
        let q_sample = textureLoad(noisy_image, q, 0);
        let q_color = q_sample.rgb;
        let q_luma = luminance(q_color);
        let q_coverage = q_sample.a;

        for (var h = 0u; h < HIDDEN_CHANNELS; h = h + 1u) {
            if RGB_CNN_MODE == 1u {
                hidden[h] = hidden[h] + q_luma * weights.values[conv0_weight_index(h, 0u, tap)];
            } else if RGB_CNN_MODE == 2u {
                hidden[h] = hidden[h]
                    + q_color.x * weights.values[conv0_weight_index(h, 0u, tap)]
                    + q_color.y * weights.values[conv0_weight_index(h, 1u, tap)]
                    + q_color.z * weights.values[conv0_weight_index(h, 2u, tap)]
                    + q_coverage * weights.values[conv0_weight_index(h, 3u, tap)];
            } else {
                hidden[h] = hidden[h]
                    + q_color.x * weights.values[conv0_weight_index(h, 0u, tap)]
                    + q_color.y * weights.values[conv0_weight_index(h, 1u, tap)]
                    + q_color.z * weights.values[conv0_weight_index(h, 2u, tap)];
            }
        }
    }

    let conv1_weight_base = rgb_cnn_conv1_weight_offset();
    let conv1_bias_base = rgb_cnn_conv1_bias_offset();
    var raw_delta = vec3<f32>(weights.values[conv1_bias_base], 0.0, 0.0);
    if RGB_CNN_MODE != 1u {
        raw_delta.y = weights.values[conv1_bias_base + 1u];
        raw_delta.z = weights.values[conv1_bias_base + 2u];
    }
    for (var h = 0u; h < HIDDEN_CHANNELS; h = h + 1u) {
        let activation = max(hidden[h], 0.0);
        if RGB_CNN_MODE == 1u {
            raw_delta.x = raw_delta.x + activation * weights.values[conv1_weight_base + h];
        } else {
            raw_delta.x = raw_delta.x + activation * weights.values[conv1_weight_base + 0u * HIDDEN_CHANNELS + h];
            raw_delta.y = raw_delta.y + activation * weights.values[conv1_weight_base + 1u * HIDDEN_CHANNELS + h];
            raw_delta.z = raw_delta.z + activation * weights.values[conv1_weight_base + 2u * HIDDEN_CHANNELS + h];
        }
    }

    if RGB_CNN_MODE == 1u {
        let delta_luma = MAX_RGB_DELTA * tanh_approx(raw_delta.x);
        let out_luma = clamp(center_luma + delta_luma, 0.0, 1.0);
        let center_chroma = center_color / max(center_luma, CHROMA_EPSILON);

        var local_color = vec3<f32>(0.0);
        for (var tap = 0u; tap < RGB_CNN_TAP_COUNT; tap = tap + 1u) {
            let offset = rgb_cnn_tap_offset(tap);
            let q = clamp_px(px + offset, size);
            local_color = local_color + textureLoad(noisy_image, q, 0).rgb;
        }
        local_color = local_color / f32(RGB_CNN_TAP_COUNT);
        let local_chroma = local_color / max(luminance(local_color), CHROMA_EPSILON);
        let chroma = select(center_chroma, local_chroma, center_luma < CHROMA_FALLBACK_LUMA);
        display_store(px, chroma * out_luma);
        return;
    }

    if RGB_CNN_MODE == 2u {
        let reconstructed = vec3<f32>(
            sigmoid_approx(raw_delta.x),
            sigmoid_approx(raw_delta.y),
            sigmoid_approx(raw_delta.z),
        );
        display_store(px, reconstructed);
        return;
    }

    let delta = MAX_RGB_DELTA * vec3<f32>(
        tanh_approx(raw_delta.x),
        tanh_approx(raw_delta.y),
        tanh_approx(raw_delta.z),
    );
    display_store(px, center_color + delta);
}
