use std::error::Error;
use std::sync::Arc;
use std::time::Instant;

use bytemuck::{Pod, Zeroable};
use winit::application::ApplicationHandler;
use winit::dpi::{LogicalSize, PhysicalPosition, PhysicalSize};
use winit::event::{ElementState, WindowEvent};
use winit::event_loop::{ActiveEventLoop, ControlFlow, EventLoop};
use winit::keyboard::{Key, NamedKey};
use winit::window::{Window, WindowAttributes, WindowId};

const WIDTH: u32 = 1280;
const HEIGHT: u32 = 720;
const TILE_SIZE: u32 = 8;

const CAMERA_ORBIT_SPEED: f32 = 0.12;
const CAMERA_ORBIT_ANGLE: f32 = 0.45;
const CAMERA_ORBIT_RADIUS: f32 = 16.0;
const CAMERA_HEIGHT: f32 = 6.0;
const CAMERA_FOCAL_LENGTH: f32 = 1.5;

const TRACE_SHADER: &str = include_str!("../resources/shaders/trace.wgsl");
const BLIT_SHADER: &str = include_str!("../resources/shaders/blit.wgsl");

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct CameraUniform {
    ro_time: [f32; 4],
    right: [f32; 4],
    up: [f32; 4],
    forward: [f32; 4],
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
    last_frame: Instant,
    stats_time: Instant,
    frame_ms_sum: f64,
    frame_ms_count: u32,
}

impl FrameStats {
    fn new() -> Self {
        let now = Instant::now();
        Self {
            start: now,
            last_frame: now,
            stats_time: now,
            frame_ms_sum: 0.0,
            frame_ms_count: 0,
        }
    }

    fn sample(&mut self) -> f32 {
        let now = Instant::now();
        let frame_ms = (now - self.last_frame).as_secs_f64() * 1000.0;
        self.last_frame = now;
        self.frame_ms_sum += frame_ms;
        self.frame_ms_count += 1;

        if (now - self.stats_time).as_secs_f64() >= 1.0 {
            let average_ms = self.frame_ms_sum / f64::from(self.frame_ms_count);
            println!(
                "frame {:.2} ms ({:.1} fps)",
                average_ms,
                1000.0 / average_ms
            );
            self.stats_time = now;
            self.frame_ms_sum = 0.0;
            self.frame_ms_count = 0;
        }

        (now - self.start).as_secs_f32()
    }
}

struct Renderer {
    instance: wgpu::Instance,
    window: Arc<Window>,
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,
    size: PhysicalSize<u32>,
    camera_buffer: wgpu::Buffer,
    compute_bind_group_layout: wgpu::BindGroupLayout,
    blit_bind_group_layout: wgpu::BindGroupLayout,
    compute_bind_group: wgpu::BindGroup,
    blit_bind_group: wgpu::BindGroup,
    compute_pipeline: wgpu::ComputePipeline,
    blit_pipeline: wgpu::RenderPipeline,
}

enum RenderOutcome {
    Drawn,
    Skip,
    Reconfigure,
    RecreateSurface,
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

        let trace_shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("trace shader"),
            source: wgpu::ShaderSource::Wgsl(TRACE_SHADER.into()),
        });
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

        let blit_shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("blit shader"),
            source: wgpu::ShaderSource::Wgsl(BLIT_SHADER.into()),
        });
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

        let (compute_bind_group, blit_bind_group) = Self::create_frame_bind_groups(
            &device,
            &camera_buffer,
            &compute_bind_group_layout,
            &blit_bind_group_layout,
            config.width,
            config.height,
        );

        Self {
            instance,
            window,
            surface,
            device,
            queue,
            config,
            size,
            camera_buffer,
            compute_bind_group_layout,
            blit_bind_group_layout,
            compute_bind_group,
            blit_bind_group,
            compute_pipeline,
            blit_pipeline,
        }
    }

    fn create_frame_bind_groups(
        device: &wgpu::Device,
        camera_buffer: &wgpu::Buffer,
        compute_layout: &wgpu::BindGroupLayout,
        blit_layout: &wgpu::BindGroupLayout,
        width: u32,
        height: u32,
    ) -> (wgpu::BindGroup, wgpu::BindGroup) {
        let offscreen = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("offscreen image"),
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
        let view = offscreen.create_view(&wgpu::TextureViewDescriptor::default());
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
                    resource: wgpu::BindingResource::TextureView(&view),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: camera_buffer.as_entire_binding(),
                },
            ],
        });
        let blit_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("blit bind group"),
            layout: blit_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&view),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::Sampler(&sampler),
                },
            ],
        });

        (compute_bind_group, blit_bind_group)
    }

    fn resize(&mut self, size: PhysicalSize<u32>) {
        self.size = size;
        if size.width == 0 || size.height == 0 {
            return;
        }

        self.config.width = size.width;
        self.config.height = size.height;
        self.surface.configure(&self.device, &self.config);
        (self.compute_bind_group, self.blit_bind_group) = Self::create_frame_bind_groups(
            &self.device,
            &self.camera_buffer,
            &self.compute_bind_group_layout,
            &self.blit_bind_group_layout,
            self.config.width,
            self.config.height,
        );
    }

    fn recreate_surface(&mut self) {
        self.surface = self
            .instance
            .create_surface(Arc::clone(&self.window))
            .expect("recreate WGPU surface");

        if self.config.width > 0 && self.config.height > 0 {
            self.surface.configure(&self.device, &self.config);
        }
    }

    fn render(&mut self, camera: &CameraUniform) -> RenderOutcome {
        if self.size.width == 0 || self.size.height == 0 {
            return RenderOutcome::Skip;
        }

        self.queue
            .write_buffer(&self.camera_buffer, 0, bytemuck::bytes_of(camera));
        let (frame, reconfigure_after_present) = match self.surface.get_current_texture() {
            wgpu::CurrentSurfaceTexture::Success(frame) => (frame, false),
            wgpu::CurrentSurfaceTexture::Suboptimal(frame) => (frame, true),
            wgpu::CurrentSurfaceTexture::Timeout
            | wgpu::CurrentSurfaceTexture::Occluded
            | wgpu::CurrentSurfaceTexture::Validation => return RenderOutcome::Skip,
            wgpu::CurrentSurfaceTexture::Outdated => return RenderOutcome::Reconfigure,
            wgpu::CurrentSurfaceTexture::Lost => return RenderOutcome::RecreateSurface,
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
        if reconfigure_after_present {
            RenderOutcome::Reconfigure
        } else {
            RenderOutcome::Drawn
        }
    }
}

struct App {
    window: Option<Arc<Window>>,
    renderer: Option<Renderer>,
    stats: FrameStats,
}

impl App {
    fn new() -> Self {
        Self {
            window: None,
            renderer: None,
            stats: FrameStats::new(),
        }
    }
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.window.is_some() {
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
        self.window = Some(window);
    }

    fn window_event(&mut self, event_loop: &ActiveEventLoop, id: WindowId, event: WindowEvent) {
        let Some(window) = self.window.as_ref() else {
            return;
        };
        if id != window.id() {
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
            WindowEvent::Resized(size) => {
                if let Some(renderer) = self.renderer.as_mut() {
                    renderer.resize(size);
                }
            }
            WindowEvent::ScaleFactorChanged { .. } => {
                if let Some(renderer) = self.renderer.as_mut() {
                    renderer.resize(window.inner_size());
                }
            }
            WindowEvent::RedrawRequested => {
                let time = self.stats.sample();
                let camera = camera_uniform(time);
                if let Some(renderer) = self.renderer.as_mut() {
                    match renderer.render(&camera) {
                        RenderOutcome::Drawn | RenderOutcome::Skip => {}
                        RenderOutcome::Reconfigure => renderer.resize(renderer.size),
                        RenderOutcome::RecreateSurface => renderer.recreate_surface(),
                    }
                }
            }
            _ => {}
        }
    }

    fn about_to_wait(&mut self, _event_loop: &ActiveEventLoop) {
        if let Some(window) = self.window.as_ref() {
            window.request_redraw();
        }
    }
}

fn window_attributes() -> WindowAttributes {
    let attributes = Window::default_attributes()
        .with_title("realtimerays")
        .with_inner_size(LogicalSize::new(f64::from(WIDTH), f64::from(HEIGHT)));

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

fn main() -> Result<(), Box<dyn Error>> {
    let event_loop = EventLoop::new()?;
    event_loop.set_control_flow(ControlFlow::Poll);

    let mut app = App::new();
    event_loop.run_app(&mut app)?;
    Ok(())
}
