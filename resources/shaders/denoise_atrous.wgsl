@group(0) @binding(0) var source_image: texture_2d<f32>;
@group(0) @binding(1) var normal_image: texture_2d<f32>;
@group(0) @binding(2) var albedo_image: texture_2d<f32>;
@group(0) @binding(3) var depth_image: texture_2d<f32>;
@group(0) @binding(4) var raw_image: texture_2d<f32>;
@group(0) @binding(21) var filtered_image: texture_storage_2d<rgba16float, write>;

@group(0) @binding(20) var final_image: texture_storage_2d<rgba8unorm, write>;

const NORMAL_PHI: f32 = 80.0;
const ALBEDO_PHI: f32 = 8.0;
const DEPTH_PHI: f32 = 180.0;
const LUMA_PHI: f32 = 45.0;
const RAW_BLEND: f32 = 0.15;
const VARIANCE_PHI: f32 = 80.0;

fn clamp_px(px: vec2<i32>, size: vec2<i32>) -> vec2<i32> {
    return clamp(px, vec2<i32>(0), size - vec2<i32>(1));
}

fn luminance(c: vec3<f32>) -> f32 {
    return dot(c, vec3<f32>(0.2126, 0.7152, 0.0722));
}

fn sqr_len3(v: vec3<f32>) -> f32 {
    return dot(v, v);
}

fn guide_chroma(px: vec2<i32>) -> vec3<f32> {
    let raw_color = textureLoad(raw_image, px, 0).rgb;
    let raw_luma = luminance(raw_color);
    let raw_chroma = raw_color / max(raw_luma, 0.0001);
    let albedo = textureLoad(albedo_image, px, 0).rgb;
    let albedo_chroma = albedo / max(luminance(albedo), 0.0001);
    let raw_confidence = smoothstep(0.02, 0.12, raw_luma);
    return mix(albedo_chroma, raw_chroma, raw_confidence);
}

fn local_raw_luma_variance(px: vec2<i32>) -> f32 {
    let size_u = textureDimensions(raw_image);
    let size = vec2<i32>(i32(size_u.x), i32(size_u.y));
    var sum_luma = 0.0;
    var sum_luma2 = 0.0;
    for (var oy = -1; oy <= 1; oy = oy + 1) {
        for (var ox = -1; ox <= 1; ox = ox + 1) {
            let q = clamp_px(px + vec2<i32>(ox, oy), size);
            let l = luminance(textureLoad(raw_image, q, 0).rgb);
            sum_luma += l;
            sum_luma2 += l * l;
        }
    }
    let mean = sum_luma / 9.0;
    return max(sum_luma2 / 9.0 - mean * mean, 0.0);
}

fn kernel_weight(offset: i32) -> f32 {
    switch abs(offset) {
        case 0: {
            return 0.375;
        }
        case 1: {
            return 0.25;
        }
        default: {
            return 0.0625;
        }
    }
}

fn edge_weight(
    center_luma: f32,
    center_normal: vec3<f32>,
    center_albedo: vec3<f32>,
    center_depth: f32,
    q_luma: f32,
    q_normal: vec3<f32>,
    q_albedo: vec3<f32>,
    q_depth: f32,
) -> f32 {
    let luma_diff = q_luma - center_luma;
    let depth_diff = q_depth - center_depth;
    let penalty =
        LUMA_PHI * luma_diff * luma_diff
        + NORMAL_PHI * sqr_len3(q_normal - center_normal)
        + ALBEDO_PHI * sqr_len3(q_albedo - center_albedo)
        + DEPTH_PHI * depth_diff * depth_diff;
    return exp(-clamp(penalty, 0.0, 20.0));
}

fn filter_luma_at_step(px: vec2<i32>, step_size: i32) -> f32 {
    let size_u = textureDimensions(source_image);
    let size = vec2<i32>(i32(size_u.x), i32(size_u.y));
    let center = clamp_px(px, size);

    let center_raw = textureLoad(raw_image, center, 0).rgb;
    let center_luma = luminance(center_raw);
    let center_normal = textureLoad(normal_image, center, 0).rgb;
    let center_albedo = textureLoad(albedo_image, center, 0).rgb;
    let center_depth = textureLoad(depth_image, center, 0).x;

    var sum_luma = 0.0;
    var sum_weight = 0.0;
    for (var oy = -2; oy <= 2; oy = oy + 1) {
        for (var ox = -2; ox <= 2; ox = ox + 1) {
            let q = clamp_px(px + vec2<i32>(ox * step_size, oy * step_size), size);
            let q_color = textureLoad(source_image, q, 0).rgb;
            let q_raw = textureLoad(raw_image, q, 0).rgb;
            let q_normal = textureLoad(normal_image, q, 0).rgb;
            let q_albedo = textureLoad(albedo_image, q, 0).rgb;
            let q_depth = textureLoad(depth_image, q, 0).x;
            let spatial = kernel_weight(ox) * kernel_weight(oy);
            let guide = edge_weight(
                center_luma,
                center_normal,
                center_albedo,
                center_depth,
                luminance(q_raw),
                q_normal,
                q_albedo,
                q_depth,
            );
            let w = spatial * guide;
            sum_luma += luminance(q_color) * w;
            sum_weight += w;
        }
    }

    return sum_luma / max(sum_weight, 0.000001);
}

fn store_filtered(id: vec3<u32>, step_size: i32) {
    let px = vec2<i32>(i32(id.x), i32(id.y));
    let size_u = textureDimensions(source_image);
    let size = vec2<i32>(i32(size_u.x), i32(size_u.y));
    if any(px >= size) {
        return;
    }

    textureStore(filtered_image, px, vec4<f32>(guide_chroma(px) * filter_luma_at_step(px, step_size), 1.0));
}

fn display_store(px: vec2<i32>, color: vec3<f32>) {
    let display = pow(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), vec3<f32>(1.0 / 2.2));
    textureStore(final_image, px, vec4<f32>(display, 1.0));
}

@compute @workgroup_size(8, 8, 1)
fn filter_step_1(@builtin(global_invocation_id) id: vec3<u32>) {
    store_filtered(id, 1);
}

@compute @workgroup_size(8, 8, 1)
fn filter_step_2(@builtin(global_invocation_id) id: vec3<u32>) {
    store_filtered(id, 2);
}

@compute @workgroup_size(8, 8, 1)
fn filter_step_4(@builtin(global_invocation_id) id: vec3<u32>) {
    store_filtered(id, 4);
}

@compute @workgroup_size(8, 8, 1)
fn filter_step_8(@builtin(global_invocation_id) id: vec3<u32>) {
    store_filtered(id, 8);
}

@compute @workgroup_size(8, 8, 1)
fn copy_main(@builtin(global_invocation_id) id: vec3<u32>) {
    let px = vec2<i32>(i32(id.x), i32(id.y));
    let size_u = textureDimensions(source_image);
    let size = vec2<i32>(i32(size_u.x), i32(size_u.y));
    if any(px >= size) {
        return;
    }

    let raw_luma = luminance(textureLoad(raw_image, px, 0).rgb);
    let filtered_luma = luminance(textureLoad(source_image, px, 0).rgb);
    let variance = local_raw_luma_variance(px);
    let raw_blend = RAW_BLEND * exp(-clamp(VARIANCE_PHI * variance, 0.0, 20.0));
    let out_luma = mix(raw_luma, filtered_luma, 1.0 - raw_blend);
    display_store(px, guide_chroma(px) * out_luma);
}
