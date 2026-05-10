struct VertexOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@group(0) @binding(0) var frame_texture: texture_2d<f32>;
@group(0) @binding(1) var frame_sampler: sampler;

@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32) -> VertexOut {
    let x = f32((vertex_index << 1u) & 2u);
    let y = f32(vertex_index & 2u);

    var out: VertexOut;
    out.position = vec4<f32>(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
    out.uv = vec2<f32>(x, y);
    return out;
}

@fragment
fn fs_main(in: VertexOut) -> @location(0) vec4<f32> {
    return textureSample(frame_texture, frame_sampler, in.uv);
}
