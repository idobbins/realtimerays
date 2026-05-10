use std::collections::BTreeMap;
use std::error::Error;
use std::fs::{self, File};
use std::io::{self, BufWriter};
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

const CAMERA_ORBIT_SPEED: f32 = 0.12;
const CAMERA_ORBIT_ANGLE: f32 = 0.45;
const CAMERA_ORBIT_RADIUS: f32 = 16.0;
const CAMERA_HEIGHT: f32 = 6.0;
const CAMERA_FOCAL_LENGTH: f32 = 1.5;

const MAT_RED: u32 = 2;
const MAT_BLUE: u32 = 4;
const MAT_LIGHT: u32 = 5;
const MAT_WOOD: u32 = 6;
const MAT_LEAVES: u32 = 7;
const MAT_WALL: u32 = 8;
const MAT_ROOF: u32 = 9;
const MAT_PATH: u32 = 10;
const MAT_GLASS: u32 = 11;

const SCENE_HEADER_WORDS: usize = 16;
const SCENE_CHUNK_WORDS: usize = 8;
const SCENE_ACCEL_BOX_WORDS: usize = 8;
const SCENE_MATERIAL_BOX_WORDS: usize = 8;
const CHUNK_SIZE: i32 = 4;

const TRACE_SHADER: &str = include_str!("../resources/shaders/trace.wgsl");
const DENOISE_SHADER: &str = include_str!("../resources/shaders/denoise.wgsl");
const BLIT_SHADER: &str = include_str!("../resources/shaders/blit.wgsl");

fn create_trusted_wgsl_shader_module(
    device: &wgpu::Device,
    label: &'static str,
    source: &'static str,
) -> wgpu::ShaderModule {
    // SAFETY: These WGSL sources are static application assets, not user input.
    // The trace shader bounds-checks its storage texture writes against
    // textureDimensions, reads only scene-buffer ranges written by this binary,
    // and uses bounded loops. The denoise shader bounds-checks storage texture
    // writes and samples a 5x5 clamped neighborhood from a bound texture. The
    // blit shader only samples a bound texture using interpolated UVs from a
    // fullscreen triangle.
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

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct CameraUniform {
    ro_time: [f32; 4],
    right: [f32; 4],
    up: [f32; 4],
    forward: [f32; 4],
}

#[derive(Clone, Copy)]
struct SceneBox {
    lo: [i32; 3],
    hi: [i32; 3],
    mat: u32,
}

const SCENE_BOXES: [SceneBox; 18] = [
    SceneBox {
        lo: [-2, 15, 1],
        hi: [3, 16, 4],
        mat: MAT_LIGHT,
    },
    SceneBox {
        lo: [-2, 0, 4],
        hi: [0, 1, 11],
        mat: MAT_PATH,
    },
    SceneBox {
        lo: [0, 0, 7],
        hi: [2, 1, 9],
        mat: MAT_PATH,
    },
    SceneBox {
        lo: [-4, 0, 9],
        hi: [-2, 1, 11],
        mat: MAT_PATH,
    },
    SceneBox {
        lo: [-2, 0, 3],
        hi: [0, 3, 5],
        mat: MAT_WOOD,
    },
    SceneBox {
        lo: [0, 2, 3],
        hi: [1, 3, 5],
        mat: MAT_GLASS,
    },
    SceneBox {
        lo: [-5, 2, 0],
        hi: [-3, 3, 2],
        mat: MAT_GLASS,
    },
    SceneBox {
        lo: [-4, 6, 1],
        hi: [-3, 9, 2],
        mat: MAT_RED,
    },
    SceneBox {
        lo: [-5, 4, -3],
        hi: [3, 5, 5],
        mat: MAT_ROOF,
    },
    SceneBox {
        lo: [-4, 5, -2],
        hi: [2, 6, 4],
        mat: MAT_ROOF,
    },
    SceneBox {
        lo: [-3, 6, -1],
        hi: [1, 7, 3],
        mat: MAT_ROOF,
    },
    SceneBox {
        lo: [-4, 0, -2],
        hi: [2, 4, 4],
        mat: MAT_WALL,
    },
    SceneBox {
        lo: [5, 0, -1],
        hi: [6, 5, 0],
        mat: MAT_WOOD,
    },
    SceneBox {
        lo: [3, 4, -3],
        hi: [8, 7, 2],
        mat: MAT_LEAVES,
    },
    SceneBox {
        lo: [4, 6, -2],
        hi: [7, 9, 1],
        mat: MAT_LEAVES,
    },
    SceneBox {
        lo: [2, 5, -2],
        hi: [4, 7, 1],
        mat: MAT_LEAVES,
    },
    SceneBox {
        lo: [-7, 0, 4],
        hi: [-6, 1, 5],
        mat: MAT_RED,
    },
    SceneBox {
        lo: [3, 0, 5],
        hi: [4, 1, 6],
        mat: MAT_BLUE,
    },
];

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

fn build_scene_words() -> Vec<u32> {
    let mut chunks = BTreeMap::<[i32; 3], [u32; 2]>::new();

    for b in SCENE_BOXES {
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

    let chunk_count = chunks.len();
    let accel_box_count = SCENE_BOXES.len();
    let material_box_count = SCENE_BOXES.len();
    let chunks_base = SCENE_HEADER_WORDS;
    let accel_boxes_base = chunks_base + chunk_count * SCENE_CHUNK_WORDS;
    let material_boxes_base = accel_boxes_base + accel_box_count * SCENE_ACCEL_BOX_WORDS;
    let total_words = material_boxes_base + material_box_count * SCENE_MATERIAL_BOX_WORDS;

    let mut words = vec![0; SCENE_HEADER_WORDS];
    words[0] = chunk_count as u32;
    words[1] = accel_box_count as u32;
    words[2] = material_box_count as u32;
    words[3] = chunks_base as u32;
    words[4] = accel_boxes_base as u32;
    words[5] = material_boxes_base as u32;
    words.reserve(total_words - words.len());

    for (chunk, mask) in chunks {
        push_i32_word(&mut words, chunk[0]);
        push_i32_word(&mut words, chunk[1]);
        push_i32_word(&mut words, chunk[2]);
        words.push(mask[0]);
        words.push(mask[1]);
        words.extend([0, 0, 0]);
    }

    for b in SCENE_BOXES {
        push_i32_word(&mut words, b.lo[0]);
        push_i32_word(&mut words, b.lo[1]);
        push_i32_word(&mut words, b.lo[2]);
        push_i32_word(&mut words, b.hi[0]);
        push_i32_word(&mut words, b.hi[1]);
        push_i32_word(&mut words, b.hi[2]);
        words.extend([0, 0]);
    }

    for b in SCENE_BOXES {
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

fn create_scene_buffer(device: &wgpu::Device) -> wgpu::Buffer {
    let scene_words = build_scene_words();
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
    scene_buffer
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

fn camera_uniform(time: f32) -> CameraUniform {
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
    compute_bind_group: wgpu::BindGroup,
    denoise_bind_group: wgpu::BindGroup,
    blit_bind_group: wgpu::BindGroup,
    compute_pipeline: wgpu::ComputePipeline,
    denoise_pipeline: wgpu::ComputePipeline,
    blit_pipeline: wgpu::RenderPipeline,
}

struct Recorder {
    device: wgpu::Device,
    queue: wgpu::Queue,
    width: u32,
    height: u32,
    camera_buffer: wgpu::Buffer,
    _scene_buffer: wgpu::Buffer,
    denoised: wgpu::Texture,
    compute_bind_group: wgpu::BindGroup,
    denoise_bind_group: wgpu::BindGroup,
    compute_pipeline: wgpu::ComputePipeline,
    denoise_pipeline: wgpu::ComputePipeline,
}

impl Renderer {
    fn new(window: Arc<Window>) -> Self {
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
        let scene_buffer = create_scene_buffer(&device);

        let compute_bind_group_layout =
            device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
                label: Some("compute bind group layout"),
                entries: &[
                    wgpu::BindGroupLayoutEntry {
                        binding: 0,
                        visibility: wgpu::ShaderStages::COMPUTE,
                        ty: wgpu::BindingType::StorageTexture {
                            access: wgpu::StorageTextureAccess::WriteOnly,
                            format: wgpu::TextureFormat::Rgba8Unorm,
                            view_dimension: wgpu::TextureViewDimension::D2,
                        },
                        count: None,
                    },
                    wgpu::BindGroupLayoutEntry {
                        binding: 1,
                        visibility: wgpu::ShaderStages::COMPUTE,
                        ty: wgpu::BindingType::Buffer {
                            ty: wgpu::BufferBindingType::Uniform,
                            has_dynamic_offset: false,
                            min_binding_size: None,
                        },
                        count: None,
                    },
                    wgpu::BindGroupLayoutEntry {
                        binding: 2,
                        visibility: wgpu::ShaderStages::COMPUTE,
                        ty: wgpu::BindingType::Buffer {
                            ty: wgpu::BufferBindingType::Storage { read_only: true },
                            has_dynamic_offset: false,
                            min_binding_size: None,
                        },
                        count: None,
                    },
                ],
            });
        let blit_bind_group_layout =
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
            });
        let denoise_bind_group_layout =
            device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
                label: Some("denoise bind group layout"),
                entries: &[
                    wgpu::BindGroupLayoutEntry {
                        binding: 0,
                        visibility: wgpu::ShaderStages::COMPUTE,
                        ty: wgpu::BindingType::Texture {
                            sample_type: wgpu::TextureSampleType::Float { filterable: false },
                            view_dimension: wgpu::TextureViewDimension::D2,
                            multisampled: false,
                        },
                        count: None,
                    },
                    wgpu::BindGroupLayoutEntry {
                        binding: 1,
                        visibility: wgpu::ShaderStages::COMPUTE,
                        ty: wgpu::BindingType::StorageTexture {
                            access: wgpu::StorageTextureAccess::WriteOnly,
                            format: wgpu::TextureFormat::Rgba8Unorm,
                            view_dimension: wgpu::TextureViewDimension::D2,
                        },
                        count: None,
                    },
                ],
            });

        let trace_shader = create_trusted_wgsl_shader_module(&device, "trace shader", TRACE_SHADER);
        let compute_pipeline_layout =
            device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
                label: Some("compute pipeline layout"),
                bind_group_layouts: &[Some(&compute_bind_group_layout)],
                immediate_size: 0,
            });
        let compute_pipeline = device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
            label: Some("trace pipeline"),
            layout: Some(&compute_pipeline_layout),
            module: &trace_shader,
            entry_point: Some("main"),
            compilation_options: Default::default(),
            cache: None,
        });

        let denoise_shader =
            create_trusted_wgsl_shader_module(&device, "denoise shader", DENOISE_SHADER);
        let denoise_pipeline_layout =
            device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
                label: Some("denoise pipeline layout"),
                bind_group_layouts: &[Some(&denoise_bind_group_layout)],
                immediate_size: 0,
            });
        let denoise_pipeline = device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
            label: Some("denoise pipeline"),
            layout: Some(&denoise_pipeline_layout),
            module: &denoise_shader,
            entry_point: Some("main"),
            compilation_options: Default::default(),
            cache: None,
        });

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

        let (compute_bind_group, denoise_bind_group, blit_bind_group) =
            Self::create_frame_bind_groups(
                &device,
                &camera_buffer,
                &scene_buffer,
                &compute_bind_group_layout,
                &denoise_bind_group_layout,
                &blit_bind_group_layout,
                config.width,
                config.height,
            );

        Self {
            window,
            surface,
            device,
            queue,
            config,
            camera_buffer,
            _scene_buffer: scene_buffer,
            compute_bind_group,
            denoise_bind_group,
            blit_bind_group,
            compute_pipeline,
            denoise_pipeline,
            blit_pipeline,
        }
    }

    fn create_frame_bind_groups(
        device: &wgpu::Device,
        camera_buffer: &wgpu::Buffer,
        scene_buffer: &wgpu::Buffer,
        compute_layout: &wgpu::BindGroupLayout,
        denoise_layout: &wgpu::BindGroupLayout,
        blit_layout: &wgpu::BindGroupLayout,
        width: u32,
        height: u32,
    ) -> (wgpu::BindGroup, wgpu::BindGroup, wgpu::BindGroup) {
        let noisy = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("noisy image"),
            size: wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::STORAGE_BINDING | wgpu::TextureUsages::TEXTURE_BINDING,
            view_formats: &[],
        });
        let denoised = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("denoised image"),
            size: wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::STORAGE_BINDING | wgpu::TextureUsages::TEXTURE_BINDING,
            view_formats: &[],
        });
        let noisy_view = noisy.create_view(&wgpu::TextureViewDescriptor::default());
        let denoised_view = denoised.create_view(&wgpu::TextureViewDescriptor::default());
        let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("blit sampler"),
            address_mode_u: wgpu::AddressMode::ClampToEdge,
            address_mode_v: wgpu::AddressMode::ClampToEdge,
            mag_filter: wgpu::FilterMode::Nearest,
            min_filter: wgpu::FilterMode::Nearest,
            ..Default::default()
        });

        let compute_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("compute bind group"),
            layout: compute_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&noisy_view),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: camera_buffer.as_entire_binding(),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: scene_buffer.as_entire_binding(),
                },
            ],
        });
        let denoise_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("denoise bind group"),
            layout: denoise_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&noisy_view),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&denoised_view),
                },
            ],
        });
        let blit_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("blit bind group"),
            layout: blit_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&denoised_view),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::Sampler(&sampler),
                },
            ],
        });

        (compute_bind_group, denoise_bind_group, blit_bind_group)
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
            pass.set_pipeline(&self.compute_pipeline);
            pass.set_bind_group(0, &self.compute_bind_group, &[]);
            pass.dispatch_workgroups(
                self.config.width.div_ceil(TILE_SIZE),
                self.config.height.div_ceil(TILE_SIZE),
                1,
            );
        }

        {
            let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
                label: Some("denoise pass"),
                timestamp_writes: None,
            });
            pass.set_pipeline(&self.denoise_pipeline);
            pass.set_bind_group(0, &self.denoise_bind_group, &[]);
            pass.dispatch_workgroups(
                self.config.width.div_ceil(TILE_SIZE),
                self.config.height.div_ceil(TILE_SIZE),
                1,
            );
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
    }
}

impl Recorder {
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
        let scene_buffer = create_scene_buffer(&device);

        let compute_bind_group_layout =
            device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
                label: Some("record compute bind group layout"),
                entries: &[
                    wgpu::BindGroupLayoutEntry {
                        binding: 0,
                        visibility: wgpu::ShaderStages::COMPUTE,
                        ty: wgpu::BindingType::StorageTexture {
                            access: wgpu::StorageTextureAccess::WriteOnly,
                            format: wgpu::TextureFormat::Rgba8Unorm,
                            view_dimension: wgpu::TextureViewDimension::D2,
                        },
                        count: None,
                    },
                    wgpu::BindGroupLayoutEntry {
                        binding: 1,
                        visibility: wgpu::ShaderStages::COMPUTE,
                        ty: wgpu::BindingType::Buffer {
                            ty: wgpu::BufferBindingType::Uniform,
                            has_dynamic_offset: false,
                            min_binding_size: None,
                        },
                        count: None,
                    },
                    wgpu::BindGroupLayoutEntry {
                        binding: 2,
                        visibility: wgpu::ShaderStages::COMPUTE,
                        ty: wgpu::BindingType::Buffer {
                            ty: wgpu::BufferBindingType::Storage { read_only: true },
                            has_dynamic_offset: false,
                            min_binding_size: None,
                        },
                        count: None,
                    },
                ],
            });
        let denoise_bind_group_layout =
            device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
                label: Some("record denoise bind group layout"),
                entries: &[
                    wgpu::BindGroupLayoutEntry {
                        binding: 0,
                        visibility: wgpu::ShaderStages::COMPUTE,
                        ty: wgpu::BindingType::Texture {
                            sample_type: wgpu::TextureSampleType::Float { filterable: false },
                            view_dimension: wgpu::TextureViewDimension::D2,
                            multisampled: false,
                        },
                        count: None,
                    },
                    wgpu::BindGroupLayoutEntry {
                        binding: 1,
                        visibility: wgpu::ShaderStages::COMPUTE,
                        ty: wgpu::BindingType::StorageTexture {
                            access: wgpu::StorageTextureAccess::WriteOnly,
                            format: wgpu::TextureFormat::Rgba8Unorm,
                            view_dimension: wgpu::TextureViewDimension::D2,
                        },
                        count: None,
                    },
                ],
            });

        let trace_shader =
            create_trusted_wgsl_shader_module(&device, "record trace shader", TRACE_SHADER);
        let compute_pipeline_layout =
            device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
                label: Some("record compute pipeline layout"),
                bind_group_layouts: &[Some(&compute_bind_group_layout)],
                immediate_size: 0,
            });
        let compute_pipeline = device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
            label: Some("record trace pipeline"),
            layout: Some(&compute_pipeline_layout),
            module: &trace_shader,
            entry_point: Some("main"),
            compilation_options: Default::default(),
            cache: None,
        });

        let denoise_shader =
            create_trusted_wgsl_shader_module(&device, "record denoise shader", DENOISE_SHADER);
        let denoise_pipeline_layout =
            device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
                label: Some("record denoise pipeline layout"),
                bind_group_layouts: &[Some(&denoise_bind_group_layout)],
                immediate_size: 0,
            });
        let denoise_pipeline = device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
            label: Some("record denoise pipeline"),
            layout: Some(&denoise_pipeline_layout),
            module: &denoise_shader,
            entry_point: Some("main"),
            compilation_options: Default::default(),
            cache: None,
        });

        let noisy = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("record noisy image"),
            size: wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::STORAGE_BINDING | wgpu::TextureUsages::TEXTURE_BINDING,
            view_formats: &[],
        });
        let denoised = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("record denoised image"),
            size: wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::STORAGE_BINDING | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        let noisy_view = noisy.create_view(&wgpu::TextureViewDescriptor::default());
        let denoised_view = denoised.create_view(&wgpu::TextureViewDescriptor::default());
        let compute_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("record compute bind group"),
            layout: &compute_bind_group_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&noisy_view),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: camera_buffer.as_entire_binding(),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: scene_buffer.as_entire_binding(),
                },
            ],
        });
        let denoise_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("record denoise bind group"),
            layout: &denoise_bind_group_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&noisy_view),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&denoised_view),
                },
            ],
        });

        Ok(Self {
            device,
            queue,
            width,
            height,
            camera_buffer,
            _scene_buffer: scene_buffer,
            denoised,
            compute_bind_group,
            denoise_bind_group,
            compute_pipeline,
            denoise_pipeline,
        })
    }

    fn capture_frame(&self, camera: &CameraUniform) -> Result<Vec<u8>, Box<dyn Error>> {
        let bytes_per_pixel = 4;
        let unpadded_bytes_per_row = self.width * bytes_per_pixel;
        let padded_bytes_per_row =
            align_to(unpadded_bytes_per_row, wgpu::COPY_BYTES_PER_ROW_ALIGNMENT);
        let output_buffer_size = u64::from(padded_bytes_per_row) * u64::from(self.height);
        let readback = self.device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("record readback"),
            size: output_buffer_size,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });

        self.queue
            .write_buffer(&self.camera_buffer, 0, bytemuck::bytes_of(camera));

        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("record frame encoder"),
            });
        {
            let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
                label: Some("record trace pass"),
                timestamp_writes: None,
            });
            pass.set_pipeline(&self.compute_pipeline);
            pass.set_bind_group(0, &self.compute_bind_group, &[]);
            pass.dispatch_workgroups(
                self.width.div_ceil(TILE_SIZE),
                self.height.div_ceil(TILE_SIZE),
                1,
            );
        }
        {
            let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
                label: Some("record denoise pass"),
                timestamp_writes: None,
            });
            pass.set_pipeline(&self.denoise_pipeline);
            pass.set_bind_group(0, &self.denoise_bind_group, &[]);
            pass.dispatch_workgroups(
                self.width.div_ceil(TILE_SIZE),
                self.height.div_ceil(TILE_SIZE),
                1,
            );
        }
        encoder.copy_texture_to_buffer(
            wgpu::TexelCopyTextureInfo {
                texture: &self.denoised,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            wgpu::TexelCopyBufferInfo {
                buffer: &readback,
                layout: wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(padded_bytes_per_row),
                    rows_per_image: Some(self.height),
                },
            },
            wgpu::Extent3d {
                width: self.width,
                height: self.height,
                depth_or_array_layers: 1,
            },
        );
        self.queue.submit([encoder.finish()]);

        let slice = readback.slice(..);
        let (sender, receiver) = mpsc::channel();
        slice.map_async(wgpu::MapMode::Read, move |result| {
            let _ = sender.send(result);
        });
        self.device.poll(wgpu::PollType::wait_indefinitely())?;
        receiver
            .recv()
            .map_err(|_| io::Error::new(io::ErrorKind::Other, "readback callback dropped"))?
            .map_err(|err| io::Error::new(io::ErrorKind::Other, err))?;

        let mapped = slice.get_mapped_range();
        let mut pixels = vec![0u8; (self.width * self.height * bytes_per_pixel) as usize];
        for y in 0..self.height as usize {
            let src_offset = y * padded_bytes_per_row as usize;
            let dst_offset = y * unpadded_bytes_per_row as usize;
            let src = &mapped[src_offset..src_offset + unpadded_bytes_per_row as usize];
            let dst = &mut pixels[dst_offset..dst_offset + unpadded_bytes_per_row as usize];
            dst.copy_from_slice(src);
        }
        drop(mapped);
        readback.unmap();

        Ok(pixels)
    }
}

struct App {
    renderer: Option<Renderer>,
    stats: FrameStats,
}

impl App {
    fn new() -> Self {
        Self {
            renderer: None,
            stats: FrameStats::new(),
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
        let renderer = Renderer::new(Arc::clone(&window));
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
                let camera = camera_uniform(time);
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

fn write_png(path: &Path, width: u32, height: u32, pixels: &[u8]) -> Result<(), Box<dyn Error>> {
    let file = File::create(path)?;
    let writer = BufWriter::new(file);
    let mut encoder = png::Encoder::new(writer, width, height);
    encoder.set_color(png::ColorType::Rgba);
    encoder.set_depth(png::BitDepth::Eight);
    encoder.write_header()?.write_image_data(pixels)?;
    Ok(())
}

fn run_record_x(args: &[String]) -> Result<(), Box<dyn Error>> {
    let output_dir = args
        .first()
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("renders/shot01"));
    let frame_count = parse_frame_count(args)?;

    fs::create_dir_all(&output_dir)?;
    let recorder = Recorder::new(WIDTH, HEIGHT)?;

    for frame_index in 0..frame_count {
        let time = frame_index as f32 / RECORD_FPS as f32;
        let pixels = recorder.capture_frame(&camera_uniform(time))?;
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
            println!("  realtimerays record-x [output-dir] [--frames N]");
            println!("  realtimerays remux [output-dir]");
            return Ok(());
        }
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

    let mut app = App::new();
    event_loop.run_app(&mut app)?;
    Ok(())
}
