@group(0) @binding(0) var noisy_image: texture_2d<f32>;
@group(0) @binding(1) var out_image: texture_storage_2d<rgba8unorm, write>;

const EDGE_STRENGTH: f32 = 5.0;
const MIN_EDGE_WEIGHT: f32 = 0.18;

fn luminance(c: vec3<f32>) -> f32 {
    return dot(c, vec3<f32>(0.2126, 0.7152, 0.0722));
}

fn sample_clamped(px: vec2<i32>, size: vec2<i32>) -> vec3<f32> {
    let clamped_px = clamp(px, vec2<i32>(0), size - vec2<i32>(1));
    return textureLoad(noisy_image, clamped_px, 0).rgb;
}

fn gaussian_weight(offset: vec2<i32>) -> f32 {
    let d = f32(dot(offset, offset));
    return exp(-d * 0.28);
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let px = vec2<i32>(i32(id.x), i32(id.y));
    let size_u = textureDimensions(out_image);
    let size = vec2<i32>(i32(size_u.x), i32(size_u.y));

    if any(px >= size) {
        return;
    }

    let center = sample_clamped(px, size);
    let center_luma = luminance(center);
    var sum = vec3<f32>(0.0);
    var weight_sum = 0.0;

    for (var y = -2; y <= 2; y = y + 1) {
        for (var x = -2; x <= 2; x = x + 1) {
            let offset = vec2<i32>(x, y);
            let sample_color = sample_clamped(px + offset, size);
            let luma_delta = luminance(sample_color) - center_luma;
            let edge_weight = max(exp(-(luma_delta * luma_delta) * EDGE_STRENGTH), MIN_EDGE_WEIGHT);
            let weight = gaussian_weight(offset) * edge_weight;
            sum += sample_color * weight;
            weight_sum += weight;
        }
    }

    let color = sum / max(weight_sum, 0.0001);
    textureStore(out_image, px, vec4<f32>(color, 1.0));
}
