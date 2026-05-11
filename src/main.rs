use std::borrow::Cow;
use std::collections::BTreeMap;
use std::error::Error;
use std::fs::{self, File};
use std::io::{self, BufWriter, Write};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::mpsc;
use std::sync::Arc;
use std::time::Instant;

use bytemuck::{Pod, Zeroable};
use winit::application::ApplicationHandler;
use winit::dpi::{LogicalSize, PhysicalPosition};
use winit::event::{ElementState, WindowEvent};
use winit::event_loop::{ActiveEventLoop, ControlFlow, EventLoop};
use winit::keyboard::{Key, NamedKey};
use winit::window::{Window, WindowAttributes, WindowId};

const WIDTH: u32 = 1280;
const HEIGHT: u32 = 720;
const TILE_SIZE: u32 = 8;
const RECORD_FPS: u32 = 60;
const RECORD_SECONDS: u32 = 10;
const RECORD_FRAME_COUNT: u32 = RECORD_FPS * RECORD_SECONDS;
const RUNTIME_SPP: u32 = 2;
const DATASET_INPUT_SPP: u32 = 2;
const DATASET_TARGET_SPP: u32 = 12;
const FILTER_DENOISER_WEIGHT_COUNT: usize = 87;
const CNN_DENOISER_WEIGHT_COUNT: usize = 737;

const CAMERA_ORBIT_SPEED: f32 = 0.12;
const CAMERA_ORBIT_ANGLE: f32 = 0.45;
const CAMERA_ORBIT_RADIUS: f32 = 34.0;
const CAMERA_HEIGHT: f32 = 13.0;
const CAMERA_FOCAL_LENGTH: f32 = 1.35;

const MAT_GRASS: u32 = 2;
const MAT_LEAF: u32 = 3;
const MAT_WOOD: u32 = 4;
const MAT_ROCK: u32 = 5;
const MAT_SNOW: u32 = 6;
const MAT_LIGHT: u32 = 7;
const MAT_WATER: u32 = 8;

const SCENE_HEADER_WORDS: usize = 16;
const SCENE_SVO_NODE_WORDS: usize = 16;
const SCENE_SVO_LEAF_WORDS: usize = 8;
const SCENE_MATERIAL_BOX_WORDS: usize = 8;
const TRACE_SVO_LEAF_THRESHOLD: usize = 64;
#[cfg(test)]
const SVO_SHADER_STACK_SIZE: usize = 32;
#[cfg(test)]
const SVO_SHADER_MAX_VISITS: usize = 256;
const CHUNK_SIZE: i32 = 4;
const MOUNTAIN_LAYERS: i32 = 11;

const TRACE_SHADER: &str = include_str!("../resources/shaders/trace.wgsl");
const TRACE_USE_SVO_DECL: &str = "const USE_SVO: bool = true;";
const TRACE_USE_SVO_BRANCH: &str = "if !USE_SVO {";
const DENOISE_SHADER: &str = include_str!("../resources/shaders/denoise.wgsl");
const DENOISE_CNN_SHADER: &str = include_str!("../resources/shaders/denoise_cnn.wgsl");
const DENOISE_ATROUS_SHADER: &str = include_str!("../resources/shaders/denoise_atrous.wgsl");
const BLIT_SHADER: &str = include_str!("../resources/shaders/blit.wgsl");

#[derive(Clone, Copy)]
enum DenoiserKind {
    Filter,
    Cnn,
    Atrous,
}

impl DenoiserKind {
    fn weight_count(self) -> Option<usize> {
        match self {
            Self::Filter => Some(FILTER_DENOISER_WEIGHT_COUNT),
            Self::Cnn => Some(CNN_DENOISER_WEIGHT_COUNT),
            Self::Atrous => None,
        }
    }

    fn weight_path(self) -> Option<&'static str> {
        match self {
            Self::Filter => Some("resources/denoiser_weights.bin"),
            Self::Cnn => Some("resources/denoiser_cnn_weights.bin"),
            Self::Atrous => None,
        }
    }

    fn shader_source(self) -> &'static str {
        match self {
            Self::Filter => DENOISE_SHADER,
            Self::Cnn => DENOISE_CNN_SHADER,
            Self::Atrous => DENOISE_ATROUS_SHADER,
        }
    }

    fn shader_label(self) -> &'static str {
        match self {
            Self::Filter => "denoise shader",
            Self::Cnn => "cnn denoise shader",
            Self::Atrous => "atrous denoise shader",
        }
    }

    fn is_single_pass(self) -> bool {
        matches!(self, Self::Filter | Self::Cnn)
    }

    fn runtime_spp(self) -> u32 {
        match self {
            Self::Atrous => 1,
            Self::Filter | Self::Cnn => RUNTIME_SPP,
        }
    }
}

fn create_trusted_wgsl_shader_module(
    device: &wgpu::Device,
    label: &str,
    source: &str,
) -> wgpu::ShaderModule {
    // SAFETY: These WGSL sources are static application assets, not user input.
    // The trace shader bounds-checks its storage texture writes against
    // textureDimensions, reads only scene-buffer ranges written by this binary,
    // and uses bounded loops. The blit shader only samples a bound texture
    // using interpolated UVs from a fullscreen triangle.
    unsafe {
        device.create_shader_module_trusted(
            wgpu::ShaderModuleDescriptor {
                label: Some(label),
                source: wgpu::ShaderSource::Wgsl(source.into()),
            },
            wgpu::ShaderRuntimeChecks::unchecked(),
        )
    }
}

fn trace_shader_source(use_svo: bool) -> Cow<'static, str> {
    if use_svo {
        Cow::Borrowed(TRACE_SHADER)
    } else {
        Cow::Owned(
            TRACE_SHADER
                .replace(TRACE_USE_SVO_DECL, "const USE_SVO: bool = false;")
                .replace(TRACE_USE_SVO_BRANCH, "if true {"),
        )
    }
}

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct CameraUniform {
    ro_time: [f32; 4],
    right: [f32; 4],
    up: [f32; 4],
    forward: [f32; 4],
    render: [u32; 4],
}

#[derive(Clone, Copy)]
struct SceneBox {
    lo: [i32; 3],
    hi: [i32; 3],
    mat: u32,
}

fn floor_div_chunk(v: i32) -> i32 {
    if v >= 0 {
        v / CHUNK_SIZE
    } else {
        -((-v + CHUNK_SIZE - 1) / CHUNK_SIZE)
    }
}

fn push_i32_word(words: &mut Vec<u32>, value: i32) {
    words.push(value as u32);
}

fn mountain_layer_bounds(y: i32) -> ([i32; 3], [i32; 3]) {
    let x_radius = 15 - y;
    let z_radius = 11 - y;
    let x_shift = y / 3 - 1;
    let z_shift = if y >= 5 { 1 } else { 0 };

    (
        [x_shift - x_radius, y, z_shift - z_radius],
        [x_shift + x_radius + 1, y + 1, z_shift + z_radius + 1],
    )
}

fn mountain_height_at(x: i32, z: i32) -> i32 {
    let mut height = 0;
    for y in 0..MOUNTAIN_LAYERS {
        let (lo, hi) = mountain_layer_bounds(y);
        if x >= lo[0] && x < hi[0] && z >= lo[2] && z < hi[2] {
            height = y + 1;
        }
    }
    height
}

fn push_tree(boxes: &mut Vec<SceneBox>, x: i32, z: i32, height: i32) {
    let y = mountain_height_at(x, z);
    boxes.push(SceneBox {
        lo: [x, y, z],
        hi: [x + 1, y + height, z + 1],
        mat: MAT_WOOD,
    });
    boxes.push(SceneBox {
        lo: [x - 2, y + height - 2, z - 2],
        hi: [x + 3, y + height, z + 3],
        mat: MAT_LEAF,
    });
    boxes.push(SceneBox {
        lo: [x - 1, y + height, z - 1],
        hi: [x + 2, y + height + 2, z + 2],
        mat: MAT_LEAF,
    });
    boxes.push(SceneBox {
        lo: [x, y + height + 2, z],
        hi: [x + 1, y + height + 3, z + 1],
        mat: MAT_LEAF,
    });
}

fn scene_boxes() -> Vec<SceneBox> {
    let mut boxes = Vec::new();

    for y in 0..MOUNTAIN_LAYERS {
        let (lo, hi) = mountain_layer_bounds(y);
        let mat = if y >= 8 {
            MAT_SNOW
        } else if y >= 3 {
            MAT_ROCK
        } else {
            MAT_GRASS
        };
        boxes.push(SceneBox { lo, hi, mat });
    }

    boxes.extend([
        SceneBox {
            lo: [-22, 0, -16],
            hi: [-9, 1, -10],
            mat: MAT_WATER,
        },
        SceneBox {
            lo: [-20, 0, -10],
            hi: [-16, 1, 4],
            mat: MAT_WATER,
        },
        SceneBox {
            lo: [13, 0, -15],
            hi: [22, 1, -11],
            mat: MAT_WATER,
        },
        SceneBox {
            lo: [-15, 1, 8],
            hi: [-12, 4, 11],
            mat: MAT_ROCK,
        },
        SceneBox {
            lo: [14, 1, 5],
            hi: [17, 3, 8],
            mat: MAT_ROCK,
        },
        SceneBox {
            lo: [3, 11, 0],
            hi: [8, 12, 3],
            mat: MAT_SNOW,
        },
    ]);

    for (x, z, height) in [
        (-20, -7, 6),
        (-18, 7, 5),
        (-12, -4, 6),
        (-9, 9, 5),
        (-2, -12, 5),
        (8, -7, 6),
        (10, 6, 5),
        (17, 9, 6),
        (20, -4, 5),
    ] {
        push_tree(&mut boxes, x, z, height);
    }

    boxes.push(SceneBox {
        lo: [-7, 16, -7],
        hi: [8, 17, 8],
        mat: MAT_LIGHT,
    });

    boxes
}

#[derive(Clone, Copy, Debug)]
struct OccupancyChunk {
    coord: [i32; 3],
    mask: [u32; 2],
}

#[derive(Clone, Copy, Debug)]
struct SvoNode {
    origin: [i32; 3],
    size: i32,
    child_mask: u32,
    child_refs: [i32; 8],
}

fn build_occupancy_chunks(scene_boxes: &[SceneBox]) -> Vec<OccupancyChunk> {
    let mut chunks = BTreeMap::<[i32; 3], [u32; 2]>::new();

    for b in scene_boxes {
        for z in b.lo[2]..b.hi[2] {
            for y in b.lo[1]..b.hi[1] {
                for x in b.lo[0]..b.hi[0] {
                    let chunk = [floor_div_chunk(x), floor_div_chunk(y), floor_div_chunk(z)];
                    let local = [
                        x - chunk[0] * CHUNK_SIZE,
                        y - chunk[1] * CHUNK_SIZE,
                        z - chunk[2] * CHUNK_SIZE,
                    ];
                    let bit = (local[0]
                        + local[1] * CHUNK_SIZE
                        + local[2] * CHUNK_SIZE * CHUNK_SIZE) as u32;
                    let mask = chunks.entry(chunk).or_insert([0; 2]);
                    if bit < 32 {
                        mask[0] |= 1 << bit;
                    } else {
                        mask[1] |= 1 << (bit - 32);
                    }
                }
            }
        }
    }

    chunks
        .into_iter()
        .map(|(coord, mask)| OccupancyChunk { coord, mask })
        .collect()
}

fn contains_chunk(origin: [i32; 3], size: i32, chunk: [i32; 3]) -> bool {
    chunk[0] >= origin[0]
        && chunk[1] >= origin[1]
        && chunk[2] >= origin[2]
        && chunk[0] < origin[0] + size
        && chunk[1] < origin[1] + size
        && chunk[2] < origin[2] + size
}

fn build_svo_subtree(
    origin: [i32; 3],
    size: i32,
    chunks: &[OccupancyChunk],
    nodes: &mut Vec<SvoNode>,
    leaves: &mut Vec<OccupancyChunk>,
) -> i32 {
    if chunks.is_empty() {
        return 0;
    }

    if size == 1 {
        debug_assert_eq!(chunks.len(), 1);
        debug_assert_eq!(chunks[0].coord, origin);
        let leaf_id = leaves.len() as i32;
        leaves.push(chunks[0]);
        return -(leaf_id + 1);
    }

    let node_id = nodes.len();
    nodes.push(SvoNode {
        origin,
        size,
        child_mask: 0,
        child_refs: [0; 8],
    });

    let child_size = size / 2;
    for child in 0..8 {
        let child_origin = [
            origin[0] + if child & 1 != 0 { child_size } else { 0 },
            origin[1] + if child & 2 != 0 { child_size } else { 0 },
            origin[2] + if child & 4 != 0 { child_size } else { 0 },
        ];
        let child_chunks = chunks
            .iter()
            .copied()
            .filter(|chunk| contains_chunk(child_origin, child_size, chunk.coord))
            .collect::<Vec<_>>();

        let child_ref = build_svo_subtree(child_origin, child_size, &child_chunks, nodes, leaves);
        if child_ref != 0 {
            nodes[node_id].child_mask |= 1u32 << child;
            nodes[node_id].child_refs[child] = child_ref;
        }
    }

    node_id as i32 + 1
}

fn build_svo(chunks: &[OccupancyChunk]) -> (Vec<SvoNode>, Vec<OccupancyChunk>, i32) {
    if chunks.is_empty() {
        return (Vec::new(), Vec::new(), 0);
    }

    let min = [
        chunks.iter().map(|chunk| chunk.coord[0]).min().unwrap(),
        chunks.iter().map(|chunk| chunk.coord[1]).min().unwrap(),
        chunks.iter().map(|chunk| chunk.coord[2]).min().unwrap(),
    ];
    let max = [
        chunks.iter().map(|chunk| chunk.coord[0]).max().unwrap(),
        chunks.iter().map(|chunk| chunk.coord[1]).max().unwrap(),
        chunks.iter().map(|chunk| chunk.coord[2]).max().unwrap(),
    ];
    let extent = [
        max[0] - min[0] + 1,
        max[1] - min[1] + 1,
        max[2] - min[2] + 1,
    ];
    let max_extent = extent.into_iter().max().unwrap();
    let root_size = (max_extent as u32).next_power_of_two() as i32;

    let mut nodes = Vec::new();
    let mut leaves = Vec::new();
    let root_ref = build_svo_subtree(min, root_size, chunks, &mut nodes, &mut leaves);
    (nodes, leaves, root_ref)
}

fn build_scene_words() -> Vec<u32> {
    let scene_boxes = scene_boxes();
    let chunks = build_occupancy_chunks(&scene_boxes);
    let (nodes, leaves, root_ref) = build_svo(&chunks);

    let node_count = nodes.len();
    let leaf_count = leaves.len();
    let material_box_count = scene_boxes.len();
    let nodes_base = SCENE_HEADER_WORDS;
    let leaves_base = nodes_base + node_count * SCENE_SVO_NODE_WORDS;
    let material_boxes_base = leaves_base + leaf_count * SCENE_SVO_LEAF_WORDS;
    let total_words = material_boxes_base + material_box_count * SCENE_MATERIAL_BOX_WORDS;

    let mut words = vec![0; SCENE_HEADER_WORDS];
    words[0] = node_count as u32;
    words[1] = leaf_count as u32;
    words[2] = material_box_count as u32;
    words[3] = nodes_base as u32;
    words[4] = leaves_base as u32;
    words[5] = material_boxes_base as u32;
    words[6] = root_ref as u32;
    words.reserve(total_words - words.len());

    for node in nodes {
        push_i32_word(&mut words, node.origin[0]);
        push_i32_word(&mut words, node.origin[1]);
        push_i32_word(&mut words, node.origin[2]);
        push_i32_word(&mut words, node.size);
        words.push(node.child_mask);
        for child_ref in node.child_refs {
            push_i32_word(&mut words, child_ref);
        }
        words.extend([0, 0, 0]);
    }

    for leaf in leaves {
        push_i32_word(&mut words, leaf.coord[0]);
        push_i32_word(&mut words, leaf.coord[1]);
        push_i32_word(&mut words, leaf.coord[2]);
        words.push(leaf.mask[0]);
        words.push(leaf.mask[1]);
        words.extend([0, 0, 0]);
    }

    for b in scene_boxes {
        push_i32_word(&mut words, b.lo[0]);
        push_i32_word(&mut words, b.lo[1]);
        push_i32_word(&mut words, b.lo[2]);
        push_i32_word(&mut words, b.hi[0]);
        push_i32_word(&mut words, b.hi[1]);
        push_i32_word(&mut words, b.hi[2]);
        words.push(b.mat);
        words.push(0);
    }

    debug_assert_eq!(words.len(), total_words);
    words
}

fn create_scene_buffer(device: &wgpu::Device) -> (wgpu::Buffer, bool) {
    let scene_words = build_scene_words();
    let use_svo = scene_words[1] as usize > TRACE_SVO_LEAF_THRESHOLD;
    let scene_buffer = device.create_buffer(&wgpu::BufferDescriptor {
        label: Some("scene buffer"),
        size: (scene_words.len() * std::mem::size_of::<u32>()) as u64,
        usage: wgpu::BufferUsages::STORAGE,
        mapped_at_creation: true,
    });
    scene_buffer
        .slice(..)
        .get_mapped_range_mut()
        .copy_from_slice(bytemuck::cast_slice(&scene_words));
    scene_buffer.unmap();
    (scene_buffer, use_svo)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn collect_reachable_leaves(root_ref: i32, nodes: &[SvoNode], leaves: &mut Vec<usize>) {
        if root_ref < 0 {
            leaves.push((-root_ref - 1) as usize);
            return;
        }

        if root_ref == 0 {
            return;
        }

        let node = nodes[(root_ref - 1) as usize];
        for child_ref in node.child_refs {
            collect_reachable_leaves(child_ref, nodes, leaves);
        }
    }

    fn max_unpruned_node_stack_use(root_ref: i32, nodes: &[SvoNode]) -> (usize, usize) {
        let mut stack = vec![root_ref];
        let mut max_stack = stack.len();
        let mut visits = 0;

        while let Some(current_ref) = stack.pop() {
            visits += 1;
            if current_ref <= 0 {
                continue;
            }

            let node = nodes[(current_ref - 1) as usize];
            for child_ref in node.child_refs {
                if child_ref > 0 {
                    stack.push(child_ref);
                }
            }
            max_stack = max_stack.max(stack.len());
        }

        (max_stack, visits)
    }

    #[test]
    fn scene_occupancy_matches_current_boxes() {
        let scene_boxes = scene_boxes();
        let chunks = build_occupancy_chunks(&scene_boxes);
        let occupied_voxels: u32 = chunks
            .iter()
            .map(|chunk| chunk.mask[0].count_ones() + chunk.mask[1].count_ones())
            .sum();

        assert!(scene_boxes.len() > 40);
        assert!(chunks.len() > TRACE_SVO_LEAF_THRESHOLD);
        assert!(occupied_voxels > 3_500);
    }

    #[test]
    fn svo_root_contains_all_chunks() {
        let scene_boxes = scene_boxes();
        let chunks = build_occupancy_chunks(&scene_boxes);
        let (nodes, _leaves, root_ref) = build_svo(&chunks);
        assert!(root_ref > 0);

        let root = nodes[(root_ref - 1) as usize];
        assert!((root.size as u32).is_power_of_two());
        for chunk in chunks {
            assert!(contains_chunk(root.origin, root.size, chunk.coord));
        }
    }

    #[test]
    fn svo_reaches_every_occupied_chunk_once() {
        let scene_boxes = scene_boxes();
        let chunks = build_occupancy_chunks(&scene_boxes);
        let (nodes, leaves, root_ref) = build_svo(&chunks);
        let mut reachable = Vec::new();
        collect_reachable_leaves(root_ref, &nodes, &mut reachable);
        reachable.sort_unstable();

        let expected = (0..leaves.len()).collect::<Vec<_>>();
        assert_eq!(reachable, expected);
        assert_eq!(leaves.len(), chunks.len());
    }

    #[test]
    fn svo_shader_stack_covers_current_scene() {
        let scene_boxes = scene_boxes();
        let chunks = build_occupancy_chunks(&scene_boxes);
        let (nodes, _leaves, root_ref) = build_svo(&chunks);
        let (max_stack, visits) = max_unpruned_node_stack_use(root_ref, &nodes);

        assert!(max_stack <= SVO_SHADER_STACK_SIZE);
        assert!(visits <= SVO_SHADER_MAX_VISITS);
    }

    #[test]
    fn serialized_scene_layout_is_consistent() {
        let words = build_scene_words();
        let node_count = words[0] as usize;
        let leaf_count = words[1] as usize;
        let material_box_count = words[2] as usize;
        let nodes_base = words[3] as usize;
        let leaves_base = words[4] as usize;
        let material_boxes_base = words[5] as usize;
        let root_ref = words[6] as i32;

        assert_eq!(nodes_base, SCENE_HEADER_WORDS);
        assert_eq!(leaves_base, nodes_base + node_count * SCENE_SVO_NODE_WORDS);
        assert_eq!(
            material_boxes_base,
            leaves_base + leaf_count * SCENE_SVO_LEAF_WORDS
        );
        assert_eq!(
            words.len(),
            material_boxes_base + material_box_count * SCENE_MATERIAL_BOX_WORDS
        );
        assert_ne!(root_ref, 0);
    }
}

fn dot3(a: [f32; 3], b: [f32; 3]) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

fn cross3(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}

fn normalize3(mut v: [f32; 3]) -> [f32; 3] {
    let inv_len = dot3(v, v).sqrt().recip();
    v[0] *= inv_len;
    v[1] *= inv_len;
    v[2] *= inv_len;
    v
}

fn camera_uniform(time: f32, sample_count: u32, seed_offset: u32) -> CameraUniform {
    let a = CAMERA_ORBIT_ANGLE + time * CAMERA_ORBIT_SPEED;
    let target = [0.0, 2.25, 0.0];
    let ro = [
        target[0] + a.sin() * CAMERA_ORBIT_RADIUS,
        target[1] + CAMERA_HEIGHT,
        target[2] + a.cos() * CAMERA_ORBIT_RADIUS,
    ];
    let mut forward = [target[0] - ro[0], target[1] - ro[1], target[2] - ro[2]];
    let world_up = [0.0, 1.0, 0.0];

    forward = normalize3(forward);
    let right = normalize3(cross3(forward, world_up));
    let up = cross3(right, forward);

    CameraUniform {
        ro_time: [ro[0], ro[1], ro[2], time],
        right: [right[0], right[1], right[2], 0.0],
        up: [up[0], up[1], up[2], 0.0],
        forward: [forward[0], forward[1], forward[2], CAMERA_FOCAL_LENGTH],
        render: [sample_count, seed_offset, 0, 0],
    }
}

struct FrameStats {
    start: Instant,
    last: Instant,
    last_print: Instant,
    ms_sum: f64,
    frames: u32,
}

impl FrameStats {
    fn new() -> Self {
        let now = Instant::now();
        Self {
            start: now,
            last: now,
            last_print: now,
            ms_sum: 0.0,
            frames: 0,
        }
    }

    fn tick(&mut self) -> f32 {
        let now = Instant::now();
        self.ms_sum += (now - self.last).as_secs_f64() * 1000.0;
        self.frames += 1;
        self.last = now;

        if (now - self.last_print).as_secs_f64() >= 1.0 {
            let ms = self.ms_sum / f64::from(self.frames);
            println!("frame {:.2} ms ({:.1} fps)", ms, 1000.0 / ms);
            self.last_print = now;
            self.ms_sum = 0.0;
            self.frames = 0;
        }

        (now - self.start).as_secs_f32()
    }
}

struct Renderer {
    window: Arc<Window>,
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,
    camera_buffer: wgpu::Buffer,
    _scene_buffer: wgpu::Buffer,
    _denoiser_weight_buffer: Option<wgpu::Buffer>,
    _frame_textures: FrameTextures,
    trace_bind_group: wgpu::BindGroup,
    denoise_bind_group: Option<wgpu::BindGroup>,
    atrous_denoiser: Option<AtrousDenoiser>,
    display_copy_bind_group: wgpu::BindGroup,
    blit_bind_group: wgpu::BindGroup,
    trace_pipeline: wgpu::ComputePipeline,
    denoise_pipeline: Option<wgpu::ComputePipeline>,
    display_copy_pipeline: wgpu::ComputePipeline,
    blit_pipeline: wgpu::RenderPipeline,
    denoiser: Option<DenoiserKind>,
    frame_index: u32,
}

struct Recorder {
    device: wgpu::Device,
    queue: wgpu::Queue,
    width: u32,
    height: u32,
    camera_buffer: wgpu::Buffer,
    _scene_buffer: wgpu::Buffer,
    _denoiser_weight_buffer: Option<wgpu::Buffer>,
    frame_textures: FrameTextures,
    trace_bind_group: wgpu::BindGroup,
    denoise_bind_group: Option<wgpu::BindGroup>,
    atrous_denoiser: Option<AtrousDenoiser>,
    trace_pipeline: wgpu::ComputePipeline,
    denoise_pipeline: Option<wgpu::ComputePipeline>,
    display_copy_bind_group: Option<wgpu::BindGroup>,
    display_copy_pipeline: Option<wgpu::ComputePipeline>,
    denoiser: Option<DenoiserKind>,
}

struct DatasetRecorder {
    device: wgpu::Device,
    queue: wgpu::Queue,
    width: u32,
    height: u32,
    input_camera_buffer: wgpu::Buffer,
    target_camera_buffer: wgpu::Buffer,
    _scene_buffer: wgpu::Buffer,
    input_textures: TraceTextures,
    target_textures: TraceTextures,
    input_trace_bind_group: wgpu::BindGroup,
    target_trace_bind_group: wgpu::BindGroup,
    input_display_bind_group: wgpu::BindGroup,
    target_display_bind_group: wgpu::BindGroup,
    trace_pipeline: wgpu::ComputePipeline,
    display_copy_pipeline: wgpu::ComputePipeline,
}

struct TraceTextures {
    color: wgpu::Texture,
    normal: wgpu::Texture,
    albedo: wgpu::Texture,
    depth: wgpu::Texture,
    display: wgpu::Texture,
}

struct FrameTextures {
    trace: TraceTextures,
    atrous_a: wgpu::Texture,
    atrous_b: wgpu::Texture,
}

struct AtrousDenoiser {
    pipelines: Vec<wgpu::ComputePipeline>,
    bind_groups: Vec<wgpu::BindGroup>,
    display_bind_group: wgpu::BindGroup,
}

fn create_texture(
    device: &wgpu::Device,
    label: &'static str,
    width: u32,
    height: u32,
    format: wgpu::TextureFormat,
    usage: wgpu::TextureUsages,
) -> wgpu::Texture {
    device.create_texture(&wgpu::TextureDescriptor {
        label: Some(label),
        size: wgpu::Extent3d {
            width,
            height,
            depth_or_array_layers: 1,
        },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format,
        usage,
        view_formats: &[],
    })
}

fn create_trace_textures(device: &wgpu::Device, width: u32, height: u32) -> TraceTextures {
    let float_usage = wgpu::TextureUsages::STORAGE_BINDING
        | wgpu::TextureUsages::TEXTURE_BINDING
        | wgpu::TextureUsages::COPY_SRC;
    TraceTextures {
        color: create_texture(
            device,
            "trace color",
            width,
            height,
            wgpu::TextureFormat::Rgba16Float,
            float_usage,
        ),
        normal: create_texture(
            device,
            "trace normal",
            width,
            height,
            wgpu::TextureFormat::Rgba16Float,
            float_usage,
        ),
        albedo: create_texture(
            device,
            "trace albedo",
            width,
            height,
            wgpu::TextureFormat::Rgba16Float,
            float_usage,
        ),
        depth: create_texture(
            device,
            "trace depth",
            width,
            height,
            wgpu::TextureFormat::Rgba16Float,
            float_usage,
        ),
        display: create_texture(
            device,
            "trace display",
            width,
            height,
            wgpu::TextureFormat::Rgba8Unorm,
            wgpu::TextureUsages::STORAGE_BINDING
                | wgpu::TextureUsages::TEXTURE_BINDING
                | wgpu::TextureUsages::COPY_SRC,
        ),
    }
}

fn create_frame_textures(device: &wgpu::Device, width: u32, height: u32) -> FrameTextures {
    let denoise_usage = wgpu::TextureUsages::STORAGE_BINDING | wgpu::TextureUsages::TEXTURE_BINDING;
    FrameTextures {
        trace: create_trace_textures(device, width, height),
        atrous_a: create_texture(
            device,
            "atrous denoise a",
            width,
            height,
            wgpu::TextureFormat::Rgba16Float,
            denoise_usage,
        ),
        atrous_b: create_texture(
            device,
            "atrous denoise b",
            width,
            height,
            wgpu::TextureFormat::Rgba16Float,
            denoise_usage,
        ),
    }
}

fn load_denoiser_weights(kind: DenoiserKind) -> Vec<f32> {
    let weight_count = kind.weight_count().expect("denoiser kind has weights");
    let path = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join(kind.weight_path().expect("denoiser kind has weights"));
    let Ok(bytes) = fs::read(&path) else {
        return vec![0.0; weight_count];
    };
    if bytes.len() != weight_count * std::mem::size_of::<f32>() {
        eprintln!(
            "ignoring {}, expected {} bytes but found {}",
            path.display(),
            weight_count * std::mem::size_of::<f32>(),
            bytes.len()
        );
        return vec![0.0; weight_count];
    }

    bytes
        .chunks_exact(4)
        .map(|chunk| f32::from_le_bytes([chunk[0], chunk[1], chunk[2], chunk[3]]))
        .collect()
}

fn create_denoiser_weight_buffer(device: &wgpu::Device, kind: DenoiserKind) -> wgpu::Buffer {
    let weights = load_denoiser_weights(kind);
    let buffer = device.create_buffer(&wgpu::BufferDescriptor {
        label: Some("denoiser weights"),
        size: (weights.len() * std::mem::size_of::<f32>()) as u64,
        usage: wgpu::BufferUsages::STORAGE,
        mapped_at_creation: true,
    });
    buffer
        .slice(..)
        .get_mapped_range_mut()
        .copy_from_slice(bytemuck::cast_slice(&weights));
    buffer.unmap();
    buffer
}

fn create_trace_bind_group_layout(device: &wgpu::Device) -> wgpu::BindGroupLayout {
    device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("trace bind group layout"),
        entries: &[
            storage_texture_entry(0, wgpu::TextureFormat::Rgba16Float),
            storage_texture_entry(1, wgpu::TextureFormat::Rgba16Float),
            storage_texture_entry(2, wgpu::TextureFormat::Rgba16Float),
            storage_texture_entry(3, wgpu::TextureFormat::Rgba16Float),
            wgpu::BindGroupLayoutEntry {
                binding: 4,
                visibility: wgpu::ShaderStages::COMPUTE,
                ty: wgpu::BindingType::Buffer {
                    ty: wgpu::BufferBindingType::Uniform,
                    has_dynamic_offset: false,
                    min_binding_size: None,
                },
                count: None,
            },
            wgpu::BindGroupLayoutEntry {
                binding: 5,
                visibility: wgpu::ShaderStages::COMPUTE,
                ty: wgpu::BindingType::Buffer {
                    ty: wgpu::BufferBindingType::Storage { read_only: true },
                    has_dynamic_offset: false,
                    min_binding_size: None,
                },
                count: None,
            },
        ],
    })
}

fn storage_texture_entry(binding: u32, format: wgpu::TextureFormat) -> wgpu::BindGroupLayoutEntry {
    wgpu::BindGroupLayoutEntry {
        binding,
        visibility: wgpu::ShaderStages::COMPUTE,
        ty: wgpu::BindingType::StorageTexture {
            access: wgpu::StorageTextureAccess::WriteOnly,
            format,
            view_dimension: wgpu::TextureViewDimension::D2,
        },
        count: None,
    }
}

fn texture_entry(binding: u32) -> wgpu::BindGroupLayoutEntry {
    wgpu::BindGroupLayoutEntry {
        binding,
        visibility: wgpu::ShaderStages::COMPUTE,
        ty: wgpu::BindingType::Texture {
            sample_type: wgpu::TextureSampleType::Float { filterable: false },
            view_dimension: wgpu::TextureViewDimension::D2,
            multisampled: false,
        },
        count: None,
    }
}

fn weights_entry() -> wgpu::BindGroupLayoutEntry {
    wgpu::BindGroupLayoutEntry {
        binding: 6,
        visibility: wgpu::ShaderStages::COMPUTE,
        ty: wgpu::BindingType::Buffer {
            ty: wgpu::BufferBindingType::Storage { read_only: true },
            has_dynamic_offset: false,
            min_binding_size: None,
        },
        count: None,
    }
}

fn create_display_copy_layout(device: &wgpu::Device) -> wgpu::BindGroupLayout {
    device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("display copy bind group layout"),
        entries: &[
            texture_entry(0),
            storage_texture_entry(20, wgpu::TextureFormat::Rgba8Unorm),
        ],
    })
}

fn create_denoise_filter_layout(device: &wgpu::Device) -> wgpu::BindGroupLayout {
    device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("denoise filter bind group layout"),
        entries: &[
            texture_entry(0),
            texture_entry(1),
            texture_entry(2),
            texture_entry(3),
            weights_entry(),
            storage_texture_entry(20, wgpu::TextureFormat::Rgba8Unorm),
        ],
    })
}

fn create_atrous_layout(device: &wgpu::Device) -> wgpu::BindGroupLayout {
    device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("atrous denoise bind group layout"),
        entries: &[
            texture_entry(0),
            texture_entry(1),
            texture_entry(2),
            texture_entry(3),
            texture_entry(4),
            storage_texture_entry(21, wgpu::TextureFormat::Rgba16Float),
        ],
    })
}

fn create_atrous_display_layout(device: &wgpu::Device) -> wgpu::BindGroupLayout {
    device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("atrous display bind group layout"),
        entries: &[
            texture_entry(0),
            texture_entry(2),
            texture_entry(4),
            storage_texture_entry(20, wgpu::TextureFormat::Rgba8Unorm),
        ],
    })
}

fn create_blit_bind_group_layout(device: &wgpu::Device) -> wgpu::BindGroupLayout {
    device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("blit bind group layout"),
        entries: &[
            wgpu::BindGroupLayoutEntry {
                binding: 0,
                visibility: wgpu::ShaderStages::FRAGMENT,
                ty: wgpu::BindingType::Texture {
                    sample_type: wgpu::TextureSampleType::Float { filterable: true },
                    view_dimension: wgpu::TextureViewDimension::D2,
                    multisampled: false,
                },
                count: None,
            },
            wgpu::BindGroupLayoutEntry {
                binding: 1,
                visibility: wgpu::ShaderStages::FRAGMENT,
                ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                count: None,
            },
        ],
    })
}

fn create_trace_bind_group(
    device: &wgpu::Device,
    layout: &wgpu::BindGroupLayout,
    textures: &TraceTextures,
    camera_buffer: &wgpu::Buffer,
    scene_buffer: &wgpu::Buffer,
) -> wgpu::BindGroup {
    let color_view = textures
        .color
        .create_view(&wgpu::TextureViewDescriptor::default());
    let normal_view = textures
        .normal
        .create_view(&wgpu::TextureViewDescriptor::default());
    let albedo_view = textures
        .albedo
        .create_view(&wgpu::TextureViewDescriptor::default());
    let depth_view = textures
        .depth
        .create_view(&wgpu::TextureViewDescriptor::default());
    device.create_bind_group(&wgpu::BindGroupDescriptor {
        label: Some("trace bind group"),
        layout,
        entries: &[
            wgpu::BindGroupEntry {
                binding: 0,
                resource: wgpu::BindingResource::TextureView(&color_view),
            },
            wgpu::BindGroupEntry {
                binding: 1,
                resource: wgpu::BindingResource::TextureView(&normal_view),
            },
            wgpu::BindGroupEntry {
                binding: 2,
                resource: wgpu::BindingResource::TextureView(&albedo_view),
            },
            wgpu::BindGroupEntry {
                binding: 3,
                resource: wgpu::BindingResource::TextureView(&depth_view),
            },
            wgpu::BindGroupEntry {
                binding: 4,
                resource: camera_buffer.as_entire_binding(),
            },
            wgpu::BindGroupEntry {
                binding: 5,
                resource: scene_buffer.as_entire_binding(),
            },
        ],
    })
}

fn create_display_copy_bind_group(
    device: &wgpu::Device,
    layout: &wgpu::BindGroupLayout,
    textures: &TraceTextures,
) -> wgpu::BindGroup {
    create_display_copy_from_texture_bind_group(device, layout, &textures.color, &textures.display)
}

fn create_display_copy_from_texture_bind_group(
    device: &wgpu::Device,
    layout: &wgpu::BindGroupLayout,
    source: &wgpu::Texture,
    display: &wgpu::Texture,
) -> wgpu::BindGroup {
    let color_view = source.create_view(&wgpu::TextureViewDescriptor::default());
    let display_view = display.create_view(&wgpu::TextureViewDescriptor::default());
    device.create_bind_group(&wgpu::BindGroupDescriptor {
        label: Some("display copy bind group"),
        layout,
        entries: &[
            wgpu::BindGroupEntry {
                binding: 0,
                resource: wgpu::BindingResource::TextureView(&color_view),
            },
            wgpu::BindGroupEntry {
                binding: 20,
                resource: wgpu::BindingResource::TextureView(&display_view),
            },
        ],
    })
}

fn create_denoise_filter_bind_group(
    device: &wgpu::Device,
    layout: &wgpu::BindGroupLayout,
    textures: &FrameTextures,
    weights: &wgpu::Buffer,
) -> wgpu::BindGroup {
    let color_view = textures
        .trace
        .color
        .create_view(&wgpu::TextureViewDescriptor::default());
    let normal_view = textures
        .trace
        .normal
        .create_view(&wgpu::TextureViewDescriptor::default());
    let albedo_view = textures
        .trace
        .albedo
        .create_view(&wgpu::TextureViewDescriptor::default());
    let depth_view = textures
        .trace
        .depth
        .create_view(&wgpu::TextureViewDescriptor::default());
    let display_view = textures
        .trace
        .display
        .create_view(&wgpu::TextureViewDescriptor::default());
    device.create_bind_group(&wgpu::BindGroupDescriptor {
        label: Some("denoise filter bind group"),
        layout,
        entries: &[
            wgpu::BindGroupEntry {
                binding: 0,
                resource: wgpu::BindingResource::TextureView(&color_view),
            },
            wgpu::BindGroupEntry {
                binding: 1,
                resource: wgpu::BindingResource::TextureView(&normal_view),
            },
            wgpu::BindGroupEntry {
                binding: 2,
                resource: wgpu::BindingResource::TextureView(&albedo_view),
            },
            wgpu::BindGroupEntry {
                binding: 3,
                resource: wgpu::BindingResource::TextureView(&depth_view),
            },
            wgpu::BindGroupEntry {
                binding: 6,
                resource: weights.as_entire_binding(),
            },
            wgpu::BindGroupEntry {
                binding: 20,
                resource: wgpu::BindingResource::TextureView(&display_view),
            },
        ],
    })
}

fn create_atrous_bind_group(
    device: &wgpu::Device,
    layout: &wgpu::BindGroupLayout,
    source: &wgpu::Texture,
    textures: &TraceTextures,
    target: &wgpu::Texture,
) -> wgpu::BindGroup {
    let source_view = source.create_view(&wgpu::TextureViewDescriptor::default());
    let normal_view = textures
        .normal
        .create_view(&wgpu::TextureViewDescriptor::default());
    let albedo_view = textures
        .albedo
        .create_view(&wgpu::TextureViewDescriptor::default());
    let depth_view = textures
        .depth
        .create_view(&wgpu::TextureViewDescriptor::default());
    let raw_view = textures
        .color
        .create_view(&wgpu::TextureViewDescriptor::default());
    let target_view = target.create_view(&wgpu::TextureViewDescriptor::default());
    device.create_bind_group(&wgpu::BindGroupDescriptor {
        label: Some("atrous denoise bind group"),
        layout,
        entries: &[
            wgpu::BindGroupEntry {
                binding: 0,
                resource: wgpu::BindingResource::TextureView(&source_view),
            },
            wgpu::BindGroupEntry {
                binding: 1,
                resource: wgpu::BindingResource::TextureView(&normal_view),
            },
            wgpu::BindGroupEntry {
                binding: 2,
                resource: wgpu::BindingResource::TextureView(&albedo_view),
            },
            wgpu::BindGroupEntry {
                binding: 3,
                resource: wgpu::BindingResource::TextureView(&depth_view),
            },
            wgpu::BindGroupEntry {
                binding: 4,
                resource: wgpu::BindingResource::TextureView(&raw_view),
            },
            wgpu::BindGroupEntry {
                binding: 21,
                resource: wgpu::BindingResource::TextureView(&target_view),
            },
        ],
    })
}

fn create_atrous_display_bind_group(
    device: &wgpu::Device,
    layout: &wgpu::BindGroupLayout,
    source: &wgpu::Texture,
    textures: &TraceTextures,
) -> wgpu::BindGroup {
    let source_view = source.create_view(&wgpu::TextureViewDescriptor::default());
    let albedo_view = textures
        .albedo
        .create_view(&wgpu::TextureViewDescriptor::default());
    let raw_view = textures
        .color
        .create_view(&wgpu::TextureViewDescriptor::default());
    let display_view = textures
        .display
        .create_view(&wgpu::TextureViewDescriptor::default());
    device.create_bind_group(&wgpu::BindGroupDescriptor {
        label: Some("atrous display bind group"),
        layout,
        entries: &[
            wgpu::BindGroupEntry {
                binding: 0,
                resource: wgpu::BindingResource::TextureView(&source_view),
            },
            wgpu::BindGroupEntry {
                binding: 2,
                resource: wgpu::BindingResource::TextureView(&albedo_view),
            },
            wgpu::BindGroupEntry {
                binding: 4,
                resource: wgpu::BindingResource::TextureView(&raw_view),
            },
            wgpu::BindGroupEntry {
                binding: 20,
                resource: wgpu::BindingResource::TextureView(&display_view),
            },
        ],
    })
}

fn create_atrous_denoiser(
    device: &wgpu::Device,
    shader: &wgpu::ShaderModule,
    atrous_layout: &wgpu::BindGroupLayout,
    atrous_display_layout: &wgpu::BindGroupLayout,
    textures: &FrameTextures,
) -> AtrousDenoiser {
    let steps = [
        ("atrous step 1 pipeline", "filter_step_1"),
        ("atrous step 2 pipeline", "filter_step_2"),
        ("atrous step 4 pipeline", "filter_step_4"),
    ];
    let pipelines = steps
        .iter()
        .map(|(label, entry)| create_compute_pipeline(device, label, shader, atrous_layout, entry))
        .collect::<Vec<_>>();
    let bind_groups = vec![
        create_atrous_bind_group(
            device,
            atrous_layout,
            &textures.trace.color,
            &textures.trace,
            &textures.atrous_a,
        ),
        create_atrous_bind_group(
            device,
            atrous_layout,
            &textures.atrous_a,
            &textures.trace,
            &textures.atrous_b,
        ),
        create_atrous_bind_group(
            device,
            atrous_layout,
            &textures.atrous_b,
            &textures.trace,
            &textures.atrous_a,
        ),
    ];
    let display_bind_group = create_atrous_display_bind_group(
        device,
        atrous_display_layout,
        &textures.atrous_a,
        &textures.trace,
    );

    AtrousDenoiser {
        pipelines,
        bind_groups,
        display_bind_group,
    }
}

fn create_blit_bind_group(
    device: &wgpu::Device,
    layout: &wgpu::BindGroupLayout,
    display: &wgpu::Texture,
) -> wgpu::BindGroup {
    let display_view = display.create_view(&wgpu::TextureViewDescriptor::default());
    let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
        label: Some("blit sampler"),
        address_mode_u: wgpu::AddressMode::ClampToEdge,
        address_mode_v: wgpu::AddressMode::ClampToEdge,
        mag_filter: wgpu::FilterMode::Nearest,
        min_filter: wgpu::FilterMode::Nearest,
        ..Default::default()
    });
    device.create_bind_group(&wgpu::BindGroupDescriptor {
        label: Some("blit bind group"),
        layout,
        entries: &[
            wgpu::BindGroupEntry {
                binding: 0,
                resource: wgpu::BindingResource::TextureView(&display_view),
            },
            wgpu::BindGroupEntry {
                binding: 1,
                resource: wgpu::BindingResource::Sampler(&sampler),
            },
        ],
    })
}

fn create_compute_pipeline(
    device: &wgpu::Device,
    label: &'static str,
    shader: &wgpu::ShaderModule,
    layout: &wgpu::BindGroupLayout,
    entry_point: &'static str,
) -> wgpu::ComputePipeline {
    let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
        label: Some(label),
        bind_group_layouts: &[Some(layout)],
        immediate_size: 0,
    });
    device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
        label: Some(label),
        layout: Some(&pipeline_layout),
        module: shader,
        entry_point: Some(entry_point),
        compilation_options: Default::default(),
        cache: None,
    })
}

impl Renderer {
    fn new(window: Arc<Window>, denoiser: Option<DenoiserKind>) -> Self {
        let size = window.inner_size();
        let mut instance_descriptor = wgpu::InstanceDescriptor::new_without_display_handle();
        instance_descriptor.backends = wgpu::Backends::METAL;
        let instance = wgpu::Instance::new(instance_descriptor);
        let surface = instance
            .create_surface(Arc::clone(&window))
            .expect("create WGPU surface");
        let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            force_fallback_adapter: false,
            compatible_surface: Some(&surface),
        }))
        .expect("request Metal adapter");

        let (device, queue) = pollster::block_on(adapter.request_device(&wgpu::DeviceDescriptor {
            label: Some("device"),
            required_features: wgpu::Features::empty(),
            required_limits: wgpu::Limits::default(),
            memory_hints: wgpu::MemoryHints::Performance,
            ..Default::default()
        }))
        .expect("request WGPU device");

        let capabilities = surface.get_capabilities(&adapter);
        let format = capabilities
            .formats
            .iter()
            .copied()
            .find(|format| *format == wgpu::TextureFormat::Bgra8Unorm)
            .or_else(|| capabilities.formats.first().copied())
            .expect("surface format");
        let present_mode = capabilities
            .present_modes
            .iter()
            .copied()
            .find(|mode| *mode == wgpu::PresentMode::Fifo)
            .unwrap_or(capabilities.present_modes[0]);
        let alpha_mode = capabilities
            .alpha_modes
            .iter()
            .copied()
            .find(|mode| *mode == wgpu::CompositeAlphaMode::Opaque)
            .unwrap_or(capabilities.alpha_modes[0]);
        let config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format,
            width: size.width.max(1),
            height: size.height.max(1),
            present_mode,
            desired_maximum_frame_latency: 2,
            alpha_mode,
            view_formats: Vec::new(),
        };
        surface.configure(&device, &config);

        let camera_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("camera uniform"),
            size: std::mem::size_of::<CameraUniform>() as u64,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let (scene_buffer, use_svo) = create_scene_buffer(&device);

        let trace_bind_group_layout = create_trace_bind_group_layout(&device);
        let denoise_filter_layout = create_denoise_filter_layout(&device);
        let atrous_layout = create_atrous_layout(&device);
        let atrous_display_layout = create_atrous_display_layout(&device);
        let display_copy_layout = create_display_copy_layout(&device);
        let blit_bind_group_layout = create_blit_bind_group_layout(&device);

        let trace_source = trace_shader_source(use_svo);
        let trace_shader =
            create_trusted_wgsl_shader_module(&device, "trace shader", &trace_source);
        let trace_pipeline = create_compute_pipeline(
            &device,
            "trace pipeline",
            &trace_shader,
            &trace_bind_group_layout,
            "main",
        );
        let denoiser_kind = denoiser.unwrap_or(DenoiserKind::Filter);
        let denoise_shader = create_trusted_wgsl_shader_module(
            &device,
            denoiser_kind.shader_label(),
            denoiser_kind.shader_source(),
        );
        let display_pipeline_layout = if matches!(denoiser, Some(DenoiserKind::Atrous)) {
            &atrous_display_layout
        } else {
            &display_copy_layout
        };
        let display_copy_pipeline = create_compute_pipeline(
            &device,
            "display copy pipeline",
            &denoise_shader,
            display_pipeline_layout,
            "copy_main",
        );

        let blit_shader = create_trusted_wgsl_shader_module(&device, "blit shader", BLIT_SHADER);
        let blit_pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("blit pipeline layout"),
            bind_group_layouts: &[Some(&blit_bind_group_layout)],
            immediate_size: 0,
        });
        let blit_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("blit pipeline"),
            layout: Some(&blit_pipeline_layout),
            vertex: wgpu::VertexState {
                module: &blit_shader,
                entry_point: Some("vs_main"),
                compilation_options: Default::default(),
                buffers: &[],
            },
            primitive: wgpu::PrimitiveState::default(),
            depth_stencil: None,
            multisample: wgpu::MultisampleState::default(),
            fragment: Some(wgpu::FragmentState {
                module: &blit_shader,
                entry_point: Some("fs_main"),
                compilation_options: Default::default(),
                targets: &[Some(wgpu::ColorTargetState {
                    format,
                    blend: None,
                    write_mask: wgpu::ColorWrites::ALL,
                })],
            }),
            multiview_mask: None,
            cache: None,
        });

        let frame_textures = create_frame_textures(&device, config.width, config.height);
        let denoiser_weight_buffer = denoiser
            .filter(|kind| kind.is_single_pass())
            .map(|kind| create_denoiser_weight_buffer(&device, kind));
        let trace_bind_group = create_trace_bind_group(
            &device,
            &trace_bind_group_layout,
            &frame_textures.trace,
            &camera_buffer,
            &scene_buffer,
        );
        let denoise_pipeline = denoiser.filter(|kind| kind.is_single_pass()).map(|_| {
            create_compute_pipeline(
                &device,
                "denoise filter pipeline",
                &denoise_shader,
                &denoise_filter_layout,
                "filter_main",
            )
        });
        let denoise_bind_group = denoiser_weight_buffer.as_ref().map(|weights| {
            create_denoise_filter_bind_group(
                &device,
                &denoise_filter_layout,
                &frame_textures,
                weights,
            )
        });
        let atrous_denoiser = if matches!(denoiser, Some(DenoiserKind::Atrous)) {
            Some(create_atrous_denoiser(
                &device,
                &denoise_shader,
                &atrous_layout,
                &atrous_display_layout,
                &frame_textures,
            ))
        } else {
            None
        };
        let display_copy_bind_group =
            create_display_copy_bind_group(&device, &display_copy_layout, &frame_textures.trace);
        let blit_bind_group = create_blit_bind_group(
            &device,
            &blit_bind_group_layout,
            &frame_textures.trace.display,
        );

        Self {
            window,
            surface,
            device,
            queue,
            config,
            camera_buffer,
            _scene_buffer: scene_buffer,
            _denoiser_weight_buffer: denoiser_weight_buffer,
            _frame_textures: frame_textures,
            trace_bind_group,
            denoise_bind_group,
            atrous_denoiser,
            display_copy_bind_group,
            blit_bind_group,
            trace_pipeline,
            denoise_pipeline,
            display_copy_pipeline,
            blit_pipeline,
            denoiser,
            frame_index: 0,
        }
    }

    fn render(&mut self, camera: &CameraUniform) {
        self.queue
            .write_buffer(&self.camera_buffer, 0, bytemuck::bytes_of(camera));
        let frame = match self.surface.get_current_texture() {
            wgpu::CurrentSurfaceTexture::Success(frame)
            | wgpu::CurrentSurfaceTexture::Suboptimal(frame) => frame,
            wgpu::CurrentSurfaceTexture::Timeout
            | wgpu::CurrentSurfaceTexture::Occluded
            | wgpu::CurrentSurfaceTexture::Validation
            | wgpu::CurrentSurfaceTexture::Outdated
            | wgpu::CurrentSurfaceTexture::Lost => return,
        };
        let frame_view = frame
            .texture
            .create_view(&wgpu::TextureViewDescriptor::default());
        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("frame encoder"),
            });

        {
            let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
                label: Some("trace pass"),
                timestamp_writes: None,
            });
            pass.set_pipeline(&self.trace_pipeline);
            pass.set_bind_group(0, &self.trace_bind_group, &[]);
            pass.dispatch_workgroups(
                self.config.width.div_ceil(TILE_SIZE),
                self.config.height.div_ceil(TILE_SIZE),
                1,
            );
        }

        match self.denoiser {
            Some(DenoiserKind::Filter | DenoiserKind::Cnn) => self.dispatch_denoiser(&mut encoder),
            Some(DenoiserKind::Atrous) => self.dispatch_atrous_denoiser(&mut encoder),
            None => self.dispatch_display_copy(&mut encoder),
        }

        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("blit pass"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &frame_view,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store,
                    },
                    depth_slice: None,
                })],
                depth_stencil_attachment: None,
                timestamp_writes: None,
                occlusion_query_set: None,
                multiview_mask: None,
            });
            pass.set_pipeline(&self.blit_pipeline);
            pass.set_bind_group(0, &self.blit_bind_group, &[]);
            pass.draw(0..3, 0..1);
        }

        self.queue.submit([encoder.finish()]);
        frame.present();
        self.frame_index = self.frame_index.wrapping_add(1);
    }

    fn dispatch_denoiser(&self, encoder: &mut wgpu::CommandEncoder) {
        let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
            label: Some("denoise pass"),
            timestamp_writes: None,
        });
        pass.set_pipeline(self.denoise_pipeline.as_ref().expect("denoise pipeline"));
        pass.set_bind_group(
            0,
            self.denoise_bind_group
                .as_ref()
                .expect("denoise bind group"),
            &[],
        );
        pass.dispatch_workgroups(
            self.config.width.div_ceil(TILE_SIZE),
            self.config.height.div_ceil(TILE_SIZE),
            1,
        );
    }

    fn dispatch_atrous_denoiser(&self, encoder: &mut wgpu::CommandEncoder) {
        let atrous = self.atrous_denoiser.as_ref().expect("atrous denoiser");
        for (pipeline, bind_group) in atrous.pipelines.iter().zip(atrous.bind_groups.iter()) {
            let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
                label: Some("atrous denoise pass"),
                timestamp_writes: None,
            });
            pass.set_pipeline(pipeline);
            pass.set_bind_group(0, bind_group, &[]);
            pass.dispatch_workgroups(
                self.config.width.div_ceil(TILE_SIZE),
                self.config.height.div_ceil(TILE_SIZE),
                1,
            );
        }
        let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
            label: Some("atrous display copy pass"),
            timestamp_writes: None,
        });
        pass.set_pipeline(&self.display_copy_pipeline);
        pass.set_bind_group(0, &atrous.display_bind_group, &[]);
        pass.dispatch_workgroups(
            self.config.width.div_ceil(TILE_SIZE),
            self.config.height.div_ceil(TILE_SIZE),
            1,
        );
    }

    fn dispatch_display_copy(&self, encoder: &mut wgpu::CommandEncoder) {
        let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
            label: Some("display copy pass"),
            timestamp_writes: None,
        });
        pass.set_pipeline(&self.display_copy_pipeline);
        pass.set_bind_group(0, &self.display_copy_bind_group, &[]);
        pass.dispatch_workgroups(
            self.config.width.div_ceil(TILE_SIZE),
            self.config.height.div_ceil(TILE_SIZE),
            1,
        );
    }
}

impl Recorder {
    fn new(
        width: u32,
        height: u32,
        denoiser: Option<DenoiserKind>,
    ) -> Result<Self, Box<dyn Error>> {
        let mut instance_descriptor = wgpu::InstanceDescriptor::new_without_display_handle();
        instance_descriptor.backends = wgpu::Backends::METAL;
        let instance = wgpu::Instance::new(instance_descriptor);
        let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            force_fallback_adapter: false,
            compatible_surface: None,
        }))
        .map_err(|_| io::Error::new(io::ErrorKind::Other, "request Metal adapter"))?;

        let (device, queue) =
            pollster::block_on(adapter.request_device(&wgpu::DeviceDescriptor {
                label: Some("record device"),
                required_features: wgpu::Features::empty(),
                required_limits: wgpu::Limits::default(),
                memory_hints: wgpu::MemoryHints::Performance,
                ..Default::default()
            }))?;

        let camera_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("record camera uniform"),
            size: std::mem::size_of::<CameraUniform>() as u64,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let (scene_buffer, use_svo) = create_scene_buffer(&device);

        let trace_layout = create_trace_bind_group_layout(&device);
        let denoise_filter_layout = create_denoise_filter_layout(&device);
        let atrous_layout = create_atrous_layout(&device);
        let atrous_display_layout = create_atrous_display_layout(&device);
        let display_copy_layout = create_display_copy_layout(&device);
        let denoiser_shader_kind = denoiser.unwrap_or(DenoiserKind::Filter);

        let trace_source = trace_shader_source(use_svo);
        let trace_shader =
            create_trusted_wgsl_shader_module(&device, "record trace shader", &trace_source);
        let trace_pipeline = create_compute_pipeline(
            &device,
            "record trace pipeline",
            &trace_shader,
            &trace_layout,
            "main",
        );
        let denoise_shader = create_trusted_wgsl_shader_module(
            &device,
            "record denoise shader",
            denoiser_shader_kind.shader_source(),
        );
        let frame_textures = create_frame_textures(&device, width, height);
        let denoiser_weight_buffer = denoiser
            .filter(|kind| kind.is_single_pass())
            .map(|kind| create_denoiser_weight_buffer(&device, kind));
        let trace_bind_group = create_trace_bind_group(
            &device,
            &trace_layout,
            &frame_textures.trace,
            &camera_buffer,
            &scene_buffer,
        );
        let denoise_pipeline = denoiser.filter(|kind| kind.is_single_pass()).map(|_| {
            create_compute_pipeline(
                &device,
                "record denoise filter pipeline",
                &denoise_shader,
                &denoise_filter_layout,
                "filter_main",
            )
        });
        let display_copy_pipeline = if matches!(denoiser, Some(DenoiserKind::Atrous)) {
            Some(create_compute_pipeline(
                &device,
                "record atrous display copy pipeline",
                &denoise_shader,
                &atrous_display_layout,
                "copy_main",
            ))
        } else if denoiser.is_none() {
            Some(create_compute_pipeline(
                &device,
                "record display copy pipeline",
                &denoise_shader,
                &display_copy_layout,
                "copy_main",
            ))
        } else {
            None
        };
        let denoise_bind_group = denoiser_weight_buffer.as_ref().map(|weights| {
            create_denoise_filter_bind_group(
                &device,
                &denoise_filter_layout,
                &frame_textures,
                weights,
            )
        });
        let display_copy_bind_group = if denoiser.is_none() {
            Some(create_display_copy_bind_group(
                &device,
                &display_copy_layout,
                &frame_textures.trace,
            ))
        } else {
            None
        };
        let atrous_denoiser = if matches!(denoiser, Some(DenoiserKind::Atrous)) {
            Some(create_atrous_denoiser(
                &device,
                &denoise_shader,
                &atrous_layout,
                &atrous_display_layout,
                &frame_textures,
            ))
        } else {
            None
        };

        Ok(Self {
            device,
            queue,
            width,
            height,
            camera_buffer,
            _scene_buffer: scene_buffer,
            _denoiser_weight_buffer: denoiser_weight_buffer,
            frame_textures,
            trace_bind_group,
            denoise_bind_group,
            atrous_denoiser,
            trace_pipeline,
            denoise_pipeline,
            display_copy_bind_group,
            display_copy_pipeline,
            denoiser,
        })
    }

    fn capture_frame(&self, camera: &CameraUniform) -> Result<Vec<u8>, Box<dyn Error>> {
        self.queue
            .write_buffer(&self.camera_buffer, 0, bytemuck::bytes_of(camera));

        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("record frame encoder"),
            });
        self.encode_frame(&mut encoder);
        let pixels = copy_texture_to_vec(
            &self.device,
            &mut encoder,
            &self.frame_textures.trace.display,
            self.width,
            self.height,
            4,
            "record readback",
        )?;
        self.queue.submit([encoder.finish()]);
        map_readback(&self.device, pixels)
    }

    fn submit_frame(&self, camera: &CameraUniform) -> Result<(), Box<dyn Error>> {
        self.queue
            .write_buffer(&self.camera_buffer, 0, bytemuck::bytes_of(camera));
        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("bench frame encoder"),
            });
        self.encode_frame(&mut encoder);
        self.queue.submit([encoder.finish()]);
        self.device.poll(wgpu::PollType::wait_indefinitely())?;
        Ok(())
    }

    fn encode_frame(&self, encoder: &mut wgpu::CommandEncoder) {
        {
            let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
                label: Some("record trace pass"),
                timestamp_writes: None,
            });
            pass.set_pipeline(&self.trace_pipeline);
            pass.set_bind_group(0, &self.trace_bind_group, &[]);
            pass.dispatch_workgroups(
                self.width.div_ceil(TILE_SIZE),
                self.height.div_ceil(TILE_SIZE),
                1,
            );
        }
        match self.denoiser {
            Some(DenoiserKind::Filter | DenoiserKind::Cnn) => self.dispatch_denoiser(encoder),
            Some(DenoiserKind::Atrous) => self.dispatch_atrous_denoiser(encoder),
            None => self.dispatch_display_copy(encoder),
        }
    }

    fn dispatch_denoiser(&self, encoder: &mut wgpu::CommandEncoder) {
        let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
            label: Some("record denoise pass"),
            timestamp_writes: None,
        });
        pass.set_pipeline(
            self.denoise_pipeline
                .as_ref()
                .expect("record denoise pipeline"),
        );
        pass.set_bind_group(
            0,
            self.denoise_bind_group
                .as_ref()
                .expect("record denoise bind group"),
            &[],
        );
        pass.dispatch_workgroups(
            self.width.div_ceil(TILE_SIZE),
            self.height.div_ceil(TILE_SIZE),
            1,
        );
    }

    fn dispatch_atrous_denoiser(&self, encoder: &mut wgpu::CommandEncoder) {
        let atrous = self
            .atrous_denoiser
            .as_ref()
            .expect("record atrous denoiser");
        for (pipeline, bind_group) in atrous.pipelines.iter().zip(atrous.bind_groups.iter()) {
            let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
                label: Some("record atrous denoise pass"),
                timestamp_writes: None,
            });
            pass.set_pipeline(pipeline);
            pass.set_bind_group(0, bind_group, &[]);
            pass.dispatch_workgroups(
                self.width.div_ceil(TILE_SIZE),
                self.height.div_ceil(TILE_SIZE),
                1,
            );
        }
        let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
            label: Some("record atrous display copy pass"),
            timestamp_writes: None,
        });
        pass.set_pipeline(
            self.display_copy_pipeline
                .as_ref()
                .expect("record atrous display copy pipeline"),
        );
        pass.set_bind_group(0, &atrous.display_bind_group, &[]);
        pass.dispatch_workgroups(
            self.width.div_ceil(TILE_SIZE),
            self.height.div_ceil(TILE_SIZE),
            1,
        );
    }

    fn dispatch_display_copy(&self, encoder: &mut wgpu::CommandEncoder) {
        let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
            label: Some("record display copy pass"),
            timestamp_writes: None,
        });
        pass.set_pipeline(
            self.display_copy_pipeline
                .as_ref()
                .expect("record display copy pipeline"),
        );
        pass.set_bind_group(
            0,
            self.display_copy_bind_group
                .as_ref()
                .expect("record display copy bind group"),
            &[],
        );
        pass.dispatch_workgroups(
            self.width.div_ceil(TILE_SIZE),
            self.height.div_ceil(TILE_SIZE),
            1,
        );
    }
}

struct DatasetFrame {
    input_color: Vec<u8>,
    input_normal: Vec<u8>,
    input_albedo: Vec<u8>,
    input_depth: Vec<u8>,
    target_color: Vec<u8>,
    debug_input: Vec<u8>,
    debug_target: Vec<u8>,
}

impl DatasetRecorder {
    fn new(width: u32, height: u32) -> Result<Self, Box<dyn Error>> {
        let mut instance_descriptor = wgpu::InstanceDescriptor::new_without_display_handle();
        instance_descriptor.backends = wgpu::Backends::METAL;
        let instance = wgpu::Instance::new(instance_descriptor);
        let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            force_fallback_adapter: false,
            compatible_surface: None,
        }))
        .map_err(|_| io::Error::new(io::ErrorKind::Other, "request Metal adapter"))?;

        let (device, queue) =
            pollster::block_on(adapter.request_device(&wgpu::DeviceDescriptor {
                label: Some("dataset device"),
                required_features: wgpu::Features::empty(),
                required_limits: wgpu::Limits::default(),
                memory_hints: wgpu::MemoryHints::Performance,
                ..Default::default()
            }))?;

        let input_camera_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("dataset input camera uniform"),
            size: std::mem::size_of::<CameraUniform>() as u64,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let target_camera_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("dataset target camera uniform"),
            size: std::mem::size_of::<CameraUniform>() as u64,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let (scene_buffer, use_svo) = create_scene_buffer(&device);
        let trace_layout = create_trace_bind_group_layout(&device);
        let display_copy_layout = create_display_copy_layout(&device);
        let trace_source = trace_shader_source(use_svo);
        let trace_shader =
            create_trusted_wgsl_shader_module(&device, "dataset trace shader", &trace_source);
        let denoise_shader =
            create_trusted_wgsl_shader_module(&device, "dataset display shader", DENOISE_SHADER);
        let trace_pipeline = create_compute_pipeline(
            &device,
            "dataset trace pipeline",
            &trace_shader,
            &trace_layout,
            "main",
        );
        let display_copy_pipeline = create_compute_pipeline(
            &device,
            "dataset display copy pipeline",
            &denoise_shader,
            &display_copy_layout,
            "copy_main",
        );
        let input_textures = create_trace_textures(&device, width, height);
        let target_textures = create_trace_textures(&device, width, height);
        let input_trace_bind_group = create_trace_bind_group(
            &device,
            &trace_layout,
            &input_textures,
            &input_camera_buffer,
            &scene_buffer,
        );
        let target_trace_bind_group = create_trace_bind_group(
            &device,
            &trace_layout,
            &target_textures,
            &target_camera_buffer,
            &scene_buffer,
        );
        let input_display_bind_group =
            create_display_copy_bind_group(&device, &display_copy_layout, &input_textures);
        let target_display_bind_group =
            create_display_copy_bind_group(&device, &display_copy_layout, &target_textures);

        Ok(Self {
            device,
            queue,
            width,
            height,
            input_camera_buffer,
            target_camera_buffer,
            _scene_buffer: scene_buffer,
            input_textures,
            target_textures,
            input_trace_bind_group,
            target_trace_bind_group,
            input_display_bind_group,
            target_display_bind_group,
            trace_pipeline,
            display_copy_pipeline,
        })
    }

    fn capture_pair(
        &self,
        input_camera: &CameraUniform,
        target_camera: &CameraUniform,
    ) -> Result<DatasetFrame, Box<dyn Error>> {
        self.queue.write_buffer(
            &self.input_camera_buffer,
            0,
            bytemuck::bytes_of(input_camera),
        );
        self.queue.write_buffer(
            &self.target_camera_buffer,
            0,
            bytemuck::bytes_of(target_camera),
        );

        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("dataset encoder"),
            });
        {
            let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
                label: Some("dataset trace pass"),
                timestamp_writes: None,
            });
            pass.set_pipeline(&self.trace_pipeline);
            pass.set_bind_group(0, &self.input_trace_bind_group, &[]);
            pass.dispatch_workgroups(
                self.width.div_ceil(TILE_SIZE),
                self.height.div_ceil(TILE_SIZE),
                1,
            );
            pass.set_bind_group(0, &self.target_trace_bind_group, &[]);
            pass.dispatch_workgroups(
                self.width.div_ceil(TILE_SIZE),
                self.height.div_ceil(TILE_SIZE),
                1,
            );
        }
        {
            let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
                label: Some("dataset display copy pass"),
                timestamp_writes: None,
            });
            pass.set_pipeline(&self.display_copy_pipeline);
            pass.set_bind_group(0, &self.input_display_bind_group, &[]);
            pass.dispatch_workgroups(
                self.width.div_ceil(TILE_SIZE),
                self.height.div_ceil(TILE_SIZE),
                1,
            );
            pass.set_bind_group(0, &self.target_display_bind_group, &[]);
            pass.dispatch_workgroups(
                self.width.div_ceil(TILE_SIZE),
                self.height.div_ceil(TILE_SIZE),
                1,
            );
        }

        let input_color = copy_texture_to_vec(
            &self.device,
            &mut encoder,
            &self.input_textures.color,
            self.width,
            self.height,
            8,
            "dataset input color readback",
        )?;
        let input_normal = copy_texture_to_vec(
            &self.device,
            &mut encoder,
            &self.input_textures.normal,
            self.width,
            self.height,
            8,
            "dataset input normal readback",
        )?;
        let input_albedo = copy_texture_to_vec(
            &self.device,
            &mut encoder,
            &self.input_textures.albedo,
            self.width,
            self.height,
            8,
            "dataset input albedo readback",
        )?;
        let input_depth = copy_texture_to_vec(
            &self.device,
            &mut encoder,
            &self.input_textures.depth,
            self.width,
            self.height,
            8,
            "dataset input depth readback",
        )?;
        let target_color = copy_texture_to_vec(
            &self.device,
            &mut encoder,
            &self.target_textures.color,
            self.width,
            self.height,
            8,
            "dataset target color readback",
        )?;
        let debug_input = copy_texture_to_vec(
            &self.device,
            &mut encoder,
            &self.input_textures.display,
            self.width,
            self.height,
            4,
            "dataset input debug readback",
        )?;
        let debug_target = copy_texture_to_vec(
            &self.device,
            &mut encoder,
            &self.target_textures.display,
            self.width,
            self.height,
            4,
            "dataset target debug readback",
        )?;
        self.queue.submit([encoder.finish()]);

        Ok(DatasetFrame {
            input_color: map_readback(&self.device, input_color)?,
            input_normal: map_readback(&self.device, input_normal)?,
            input_albedo: map_readback(&self.device, input_albedo)?,
            input_depth: map_readback(&self.device, input_depth)?,
            target_color: map_readback(&self.device, target_color)?,
            debug_input: map_readback(&self.device, debug_input)?,
            debug_target: map_readback(&self.device, debug_target)?,
        })
    }
}

struct App {
    renderer: Option<Renderer>,
    stats: FrameStats,
    denoiser: Option<DenoiserKind>,
    sample_count_override: Option<u32>,
}

impl App {
    fn new(denoiser: Option<DenoiserKind>, sample_count_override: Option<u32>) -> Self {
        Self {
            renderer: None,
            stats: FrameStats::new(),
            denoiser,
            sample_count_override,
        }
    }
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.renderer.is_some() {
            return;
        }

        event_loop.set_control_flow(ControlFlow::Poll);
        let window = Arc::new(
            event_loop
                .create_window(window_attributes())
                .expect("create window"),
        );
        center_window(&window);
        let renderer = Renderer::new(Arc::clone(&window), self.denoiser);
        window.request_redraw();

        self.renderer = Some(renderer);
    }

    fn window_event(&mut self, event_loop: &ActiveEventLoop, id: WindowId, event: WindowEvent) {
        let Some(renderer) = self.renderer.as_mut() else {
            return;
        };
        if id != renderer.window.id() {
            return;
        }

        match event {
            WindowEvent::CloseRequested => event_loop.exit(),
            WindowEvent::KeyboardInput { event, .. }
                if event.state == ElementState::Pressed
                    && matches!(event.logical_key, Key::Named(NamedKey::Escape)) =>
            {
                event_loop.exit();
            }
            WindowEvent::RedrawRequested => {
                let time = self.stats.tick();
                let seed_offset = renderer.frame_index;
                let sample_count = self.sample_count_override.unwrap_or_else(|| {
                    renderer
                        .denoiser
                        .map(DenoiserKind::runtime_spp)
                        .unwrap_or(RUNTIME_SPP)
                });
                let camera = camera_uniform(time, sample_count, seed_offset);
                renderer.render(&camera);
            }
            _ => {}
        }
    }

    fn about_to_wait(&mut self, _event_loop: &ActiveEventLoop) {
        if let Some(renderer) = self.renderer.as_ref() {
            renderer.window.request_redraw();
        }
    }
}

fn window_attributes() -> WindowAttributes {
    let attributes = Window::default_attributes()
        .with_title("realtimerays")
        .with_inner_size(LogicalSize::new(f64::from(WIDTH), f64::from(HEIGHT)))
        .with_resizable(false);

    #[cfg(target_os = "macos")]
    {
        use winit::platform::macos::WindowAttributesExtMacOS;
        attributes
            .with_fullsize_content_view(true)
            .with_title_hidden(true)
            .with_titlebar_transparent(true)
            .with_disallow_hidpi(!retina_enabled())
    }

    #[cfg(not(target_os = "macos"))]
    {
        attributes
    }
}

#[cfg(target_os = "macos")]
fn retina_enabled() -> bool {
    std::env::var("REALTIMERAYS_RETINA").is_ok_and(|value| !value.is_empty() && value != "0")
}

fn center_window(window: &Window) {
    let Some(monitor) = window.current_monitor() else {
        return;
    };
    let monitor_position = monitor.position();
    let monitor_size = monitor.size();
    let window_size = window.outer_size();
    let x = monitor_position.x + (monitor_size.width.saturating_sub(window_size.width) / 2) as i32;
    let y =
        monitor_position.y + (monitor_size.height.saturating_sub(window_size.height) / 2) as i32;
    window.set_outer_position(PhysicalPosition::new(x, y));
}

fn align_to(value: u32, alignment: u32) -> u32 {
    value.div_ceil(alignment) * alignment
}

struct PendingReadback {
    buffer: wgpu::Buffer,
    padded_bytes_per_row: u32,
    unpadded_bytes_per_row: u32,
    height: u32,
}

fn copy_texture_to_vec(
    device: &wgpu::Device,
    encoder: &mut wgpu::CommandEncoder,
    texture: &wgpu::Texture,
    width: u32,
    height: u32,
    bytes_per_pixel: u32,
    label: &'static str,
) -> Result<PendingReadback, Box<dyn Error>> {
    let unpadded_bytes_per_row = width * bytes_per_pixel;
    let padded_bytes_per_row = align_to(unpadded_bytes_per_row, wgpu::COPY_BYTES_PER_ROW_ALIGNMENT);
    let output_buffer_size = u64::from(padded_bytes_per_row) * u64::from(height);
    let buffer = device.create_buffer(&wgpu::BufferDescriptor {
        label: Some(label),
        size: output_buffer_size,
        usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
        mapped_at_creation: false,
    });

    encoder.copy_texture_to_buffer(
        wgpu::TexelCopyTextureInfo {
            texture,
            mip_level: 0,
            origin: wgpu::Origin3d::ZERO,
            aspect: wgpu::TextureAspect::All,
        },
        wgpu::TexelCopyBufferInfo {
            buffer: &buffer,
            layout: wgpu::TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(padded_bytes_per_row),
                rows_per_image: Some(height),
            },
        },
        wgpu::Extent3d {
            width,
            height,
            depth_or_array_layers: 1,
        },
    );

    Ok(PendingReadback {
        buffer,
        padded_bytes_per_row,
        unpadded_bytes_per_row,
        height,
    })
}

fn map_readback(
    device: &wgpu::Device,
    pending: PendingReadback,
) -> Result<Vec<u8>, Box<dyn Error>> {
    let slice = pending.buffer.slice(..);
    let (sender, receiver) = mpsc::channel();
    slice.map_async(wgpu::MapMode::Read, move |result| {
        let _ = sender.send(result);
    });
    device.poll(wgpu::PollType::wait_indefinitely())?;
    receiver
        .recv()
        .map_err(|_| io::Error::new(io::ErrorKind::Other, "readback callback dropped"))?
        .map_err(|err| io::Error::new(io::ErrorKind::Other, err))?;

    let mapped = slice.get_mapped_range();
    let mut pixels = vec![0u8; (pending.unpadded_bytes_per_row * pending.height) as usize];
    for y in 0..pending.height as usize {
        let src_offset = y * pending.padded_bytes_per_row as usize;
        let dst_offset = y * pending.unpadded_bytes_per_row as usize;
        let src = &mapped[src_offset..src_offset + pending.unpadded_bytes_per_row as usize];
        let dst = &mut pixels[dst_offset..dst_offset + pending.unpadded_bytes_per_row as usize];
        dst.copy_from_slice(src);
    }
    drop(mapped);
    pending.buffer.unmap();

    Ok(pixels)
}

fn write_png(path: &Path, width: u32, height: u32, pixels: &[u8]) -> Result<(), Box<dyn Error>> {
    let file = File::create(path)?;
    let writer = BufWriter::new(file);
    let mut encoder = png::Encoder::new(writer, width, height);
    encoder.set_color(png::ColorType::Rgba);
    encoder.set_depth(png::BitDepth::Eight);
    encoder.write_header()?.write_image_data(pixels)?;
    Ok(())
}

fn write_bytes(path: &Path, bytes: &[u8]) -> Result<(), Box<dyn Error>> {
    let mut file = File::create(path)?;
    file.write_all(bytes)?;
    Ok(())
}

fn run_record_x(args: &[String]) -> Result<(), Box<dyn Error>> {
    let output_dir = args
        .first()
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("renders/shot01"));
    let frame_count = parse_frame_count(args)?;
    let denoiser = parse_optional_denoiser_kind(args);
    let default_spp = denoiser
        .map(DenoiserKind::runtime_spp)
        .unwrap_or(RUNTIME_SPP);
    let sample_count = parse_sample_count(args, default_spp)?;

    fs::create_dir_all(&output_dir)?;
    let recorder = Recorder::new(WIDTH, HEIGHT, denoiser)?;

    for frame_index in 0..frame_count {
        let time = frame_index as f32 / RECORD_FPS as f32;
        let pixels = recorder.capture_frame(&camera_uniform(time, sample_count, frame_index))?;
        let path = output_dir.join(format!("frame_{frame_index:06}.png"));
        write_png(&path, WIDTH, HEIGHT, &pixels)?;

        if frame_index % RECORD_FPS == 0 || frame_index + 1 == frame_count {
            println!(
                "recorded {}/{} frames to {}",
                frame_index + 1,
                frame_count,
                output_dir.display()
            );
        }
    }

    run_remux(&output_dir)?;
    Ok(())
}

fn run_dataset(args: &[String]) -> Result<(), Box<dyn Error>> {
    let output_dir = args
        .first()
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("renders/denoiser_dataset"));
    let frame_count = parse_frame_count(args)?;

    fs::create_dir_all(&output_dir)?;
    let meta = format!(
        "{{\n  \"width\": {WIDTH},\n  \"height\": {HEIGHT},\n  \"input_spp\": {DATASET_INPUT_SPP},\n  \"target_spp\": {DATASET_TARGET_SPP},\n  \"frames\": {frame_count},\n  \"format\": \"rgba16float little-endian raw files plus rgba8 debug pngs\",\n  \"channels\": [\"input_color\", \"input_normal\", \"input_albedo\", \"input_depth\", \"target_color\"]\n}}\n"
    );
    write_bytes(&output_dir.join("dataset_meta.json"), meta.as_bytes())?;

    let recorder = DatasetRecorder::new(WIDTH, HEIGHT)?;
    for frame_index in 0..frame_count {
        let time = frame_index as f32 / RECORD_FPS as f32;
        let input_camera = camera_uniform(time, DATASET_INPUT_SPP, frame_index);
        let target_camera = camera_uniform(time, DATASET_TARGET_SPP, frame_index);
        let frame = recorder.capture_pair(&input_camera, &target_camera)?;

        write_bytes(
            &output_dir.join(format!("input_color_{frame_index:06}.rgba16f")),
            &frame.input_color,
        )?;
        write_bytes(
            &output_dir.join(format!("input_normal_{frame_index:06}.rgba16f")),
            &frame.input_normal,
        )?;
        write_bytes(
            &output_dir.join(format!("input_albedo_{frame_index:06}.rgba16f")),
            &frame.input_albedo,
        )?;
        write_bytes(
            &output_dir.join(format!("input_depth_{frame_index:06}.rgba16f")),
            &frame.input_depth,
        )?;
        write_bytes(
            &output_dir.join(format!("target_color_{frame_index:06}.rgba16f")),
            &frame.target_color,
        )?;
        write_png(
            &output_dir.join(format!("debug_input_{frame_index:06}.png")),
            WIDTH,
            HEIGHT,
            &frame.debug_input,
        )?;
        write_png(
            &output_dir.join(format!("debug_target_{frame_index:06}.png")),
            WIDTH,
            HEIGHT,
            &frame.debug_target,
        )?;

        if frame_index % RECORD_FPS == 0 || frame_index + 1 == frame_count {
            println!(
                "captured {}/{} training pairs to {}",
                frame_index + 1,
                frame_count,
                output_dir.display()
            );
        }
    }

    Ok(())
}

fn parse_arg_u32(args: &[String], name: &str, default: u32) -> Result<u32, Box<dyn Error>> {
    let Some(index) = args.iter().position(|arg| arg == name) else {
        return Ok(default);
    };
    let value = args.get(index + 1).ok_or_else(|| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            format!("{name} requires a value"),
        )
    })?;
    let parsed = value.parse::<u32>()?;
    if parsed == 0 {
        return Err(
            io::Error::new(io::ErrorKind::InvalidInput, format!("{name} must be > 0")).into(),
        );
    }
    Ok(parsed)
}

fn parse_sample_count(args: &[String], default: u32) -> Result<u32, Box<dyn Error>> {
    parse_arg_u32(args, "--spp", default)
}

fn parse_sample_count_override(args: &[String]) -> Result<Option<u32>, Box<dyn Error>> {
    if args.iter().any(|arg| arg == "--spp") {
        Ok(Some(parse_arg_u32(args, "--spp", RUNTIME_SPP)?))
    } else {
        Ok(None)
    }
}

fn parse_denoiser_kind(args: &[String]) -> DenoiserKind {
    if args.iter().any(|arg| arg == "--atrous-denoise") {
        DenoiserKind::Atrous
    } else if args.iter().any(|arg| arg == "--cnn-denoise") {
        DenoiserKind::Cnn
    } else {
        DenoiserKind::Filter
    }
}

fn parse_optional_denoiser_kind(args: &[String]) -> Option<DenoiserKind> {
    if args.iter().any(|arg| arg == "--no-denoise") {
        None
    } else {
        Some(parse_denoiser_kind(args))
    }
}

fn run_bench(args: &[String]) -> Result<(), Box<dyn Error>> {
    let frames = parse_arg_u32(args, "--frames", 120)?;
    let warmup = parse_arg_u32(args, "--warmup", 10)?;
    let denoiser = parse_optional_denoiser_kind(args);
    let default_spp = denoiser
        .map(DenoiserKind::runtime_spp)
        .unwrap_or(RUNTIME_SPP);
    let sample_count = parse_sample_count(args, default_spp)?;
    let recorder = Recorder::new(WIDTH, HEIGHT, denoiser)?;

    for frame_index in 0..warmup {
        let time = frame_index as f32 / RECORD_FPS as f32;
        recorder.submit_frame(&camera_uniform(time, sample_count, frame_index))?;
    }

    let start = Instant::now();
    for frame_index in 0..frames {
        let time = (warmup + frame_index) as f32 / RECORD_FPS as f32;
        recorder.submit_frame(&camera_uniform(time, sample_count, warmup + frame_index))?;
    }
    let seconds = start.elapsed().as_secs_f64();
    let ms = seconds * 1000.0 / f64::from(frames);
    println!(
        "bench frames={} warmup={} avg_ms={:.3} fps={:.1}",
        frames,
        warmup,
        ms,
        1000.0 / ms
    );
    Ok(())
}

fn parse_frame_count(args: &[String]) -> Result<u32, Box<dyn Error>> {
    let Some(frames_index) = args.iter().position(|arg| arg == "--frames") else {
        return Ok(RECORD_FRAME_COUNT);
    };
    let value = args.get(frames_index + 1).ok_or_else(|| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            "--frames requires a frame count",
        )
    })?;
    let frame_count = value.parse::<u32>()?;
    if frame_count == 0 {
        return Err(io::Error::new(io::ErrorKind::InvalidInput, "--frames must be > 0").into());
    }
    Ok(frame_count)
}

fn run_remux(output_dir: &Path) -> Result<(), Box<dyn Error>> {
    let input = output_dir.join("frame_%06d.png");
    let output = output_dir.join("x_upload.mp4");
    let status = Command::new("ffmpeg")
        .args([
            "-y",
            "-framerate",
            &RECORD_FPS.to_string(),
            "-i",
            input
                .to_str()
                .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "non-UTF-8 path"))?,
            "-c:v",
            "libx264",
            "-profile:v",
            "high",
            "-level",
            "4.2",
            "-pix_fmt",
            "yuv420p",
            "-r",
            &RECORD_FPS.to_string(),
            "-b:v",
            "20000k",
            "-maxrate",
            "25000k",
            "-bufsize",
            "50000k",
            "-movflags",
            "+faststart",
            output
                .to_str()
                .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "non-UTF-8 path"))?,
        ])
        .status()?;

    if !status.success() {
        return Err(io::Error::new(io::ErrorKind::Other, "ffmpeg remux failed").into());
    }

    println!("wrote {}", output.display());
    Ok(())
}

fn main() -> Result<(), Box<dyn Error>> {
    let args = std::env::args().skip(1).collect::<Vec<_>>();
    match args.first().map(String::as_str) {
        Some("record-x") | Some("--record-x") => return run_record_x(&args[1..]),
        Some("dataset") => return run_dataset(&args[1..]),
        Some("bench") => return run_bench(&args[1..]),
        Some("remux") => {
            let output_dir = args
                .get(1)
                .map(PathBuf::from)
                .unwrap_or_else(|| PathBuf::from("renders/shot01"));
            return run_remux(&output_dir);
        }
        Some("--help") | Some("-h") => {
            println!("usage:");
            println!("  realtimerays");
            println!("  realtimerays --no-denoise");
            println!("  realtimerays --cnn-denoise");
            println!("  realtimerays --atrous-denoise");
            println!("  realtimerays --spp N");
            println!(
                "  realtimerays bench [--frames N] [--warmup N] [--spp N] [--no-denoise|--cnn-denoise|--atrous-denoise]"
            );
            println!("  realtimerays dataset [output-dir] [--frames N]");
            println!(
                "  realtimerays record-x [output-dir] [--frames N] [--spp N] [--no-denoise|--cnn-denoise|--atrous-denoise]"
            );
            println!("  realtimerays remux [output-dir]");
            return Ok(());
        }
        Some("--no-denoise") => {}
        Some("--cnn-denoise") => {}
        Some("--atrous-denoise") => {}
        Some("--spp") => {}
        Some(other) => {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("unknown command: {other}"),
            )
            .into());
        }
        None => {}
    }

    let event_loop = EventLoop::new()?;
    event_loop.set_control_flow(ControlFlow::Poll);

    let denoiser = if args.iter().any(|arg| arg == "--no-denoise") {
        None
    } else if args.iter().any(|arg| arg == "--atrous-denoise") {
        Some(DenoiserKind::Atrous)
    } else if args.iter().any(|arg| arg == "--cnn-denoise") {
        Some(DenoiserKind::Cnn)
    } else {
        Some(DenoiserKind::Filter)
    };
    let sample_count_override = parse_sample_count_override(&args)?;
    let mut app = App::new(denoiser, sample_count_override);
    event_loop.run_app(&mut app)?;
    Ok(())
}
