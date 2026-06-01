#![allow(static_mut_refs)]

use crate::macos;
use crate::vk::{self, VkResult};
use realtimerays::scene;
use std::ffi::{c_char, c_int, c_void, CStr};
use std::io::Write;
use std::mem::{size_of, size_of_val};
use std::time::Instant;

include!(concat!(env!("OUT_DIR"), "/trace_comp_spv.rs"));

const RTR_MAX_SWAP_IMAGES: usize = 3;
const RTR_TILE_SIZE: u32 = 8;
const RTR_MEMORY_HEADER_WORDS: u64 = 32;
const RTR_HISTORY_WORDS_PER_PIXEL: u64 = 20;
const RTR_HISTORY_PAGE_COUNT: u64 = 2;
const RTR_MEMORY_MAGIC: u32 = 0x30525452;
const RTR_TIMING_WINDOW: usize = 100;

const RTR_MEMORY_MAGIC_WORD: usize = 0;
const RTR_MEMORY_VERSION_WORD: usize = 1;
const RTR_MEMORY_WIDTH_WORD: usize = 2;
const RTR_MEMORY_HEIGHT_WORD: usize = 3;
const RTR_MEMORY_FRAME_WORD: usize = 4;
const RTR_MEMORY_MOUSE_X_WORD: usize = 5;
const RTR_MEMORY_MOUSE_Y_WORD: usize = 6;
const RTR_MEMORY_TIME_WORD: usize = 7;
const RTR_MEMORY_PREV_TIME_WORD: usize = 8;
const RTR_MEMORY_PREV_MOUSE_X_WORD: usize = 9;
const RTR_MEMORY_PREV_MOUSE_Y_WORD: usize = 10;
const RTR_MEMORY_BVH_NODE_OFFSET_WORD: usize = 11;
const RTR_MEMORY_BVH_NODE_COUNT_WORD: usize = 12;
const RTR_MEMORY_TRIANGLE_OFFSET_WORD: usize = 13;
const RTR_MEMORY_TRIANGLE_COUNT_WORD: usize = 14;
const RTR_MEMORY_HISTORY_OFFSET_WORD: usize = 15;

const CLOCK_MONOTONIC: c_int = 6;

#[repr(C)]
pub struct FILE {
    _private: [u8; 0],
}

#[repr(C)]
struct Timespec {
    tv_sec: i64,
    tv_nsec: i64,
}

unsafe extern "C" {
    static mut __stdoutp: *mut FILE;
    fn fflush(stream: *mut FILE) -> c_int;
    fn printf(format: *const c_char, ...) -> c_int;
    fn clock_gettime(clock_id: c_int, tp: *mut Timespec) -> c_int;
}

static mut RTR_INSTANCE: Option<vk::InstanceCommands> = None;
static mut RTR_SURFACE_LOADER: Option<vk::SurfaceLoader> = None;
static mut RTR_SWAPCHAIN_LOADER: Option<vk::SwapchainLoader> = None;
static mut RTR_SURFACE: vk::SurfaceKHR = vk::SurfaceKHR::null();
static mut RTR_PHYSICAL_DEVICE: vk::PhysicalDevice = vk::PhysicalDevice::null();
static mut RTR_DEVICE: Option<vk::DeviceCommands> = None;
static mut RTR_QUEUE: vk::Queue = vk::Queue::null();
static mut RTR_SWAPCHAIN: vk::SwapchainKHR = vk::SwapchainKHR::null();
static mut RTR_SWAP_EXTENT: vk::Extent2D = vk::Extent2D {
    width: 0,
    height: 0,
};
static mut RTR_SWAP_IMAGES: [vk::Image; RTR_MAX_SWAP_IMAGES] =
    [vk::Image::null(); RTR_MAX_SWAP_IMAGES];
static mut RTR_SWAP_IMAGE_VIEWS: [vk::ImageView; RTR_MAX_SWAP_IMAGES] =
    [vk::ImageView::null(); RTR_MAX_SWAP_IMAGES];
static mut RTR_DESCRIPTOR_SET_LAYOUT: vk::DescriptorSetLayout = vk::DescriptorSetLayout::null();
static mut RTR_DESCRIPTOR_POOL: vk::DescriptorPool = vk::DescriptorPool::null();
static mut RTR_DESCRIPTOR_SETS: [vk::DescriptorSet; RTR_MAX_SWAP_IMAGES] =
    [vk::DescriptorSet::null(); RTR_MAX_SWAP_IMAGES];
static mut RTR_PIPELINE_LAYOUT: vk::PipelineLayout = vk::PipelineLayout::null();
static mut RTR_PIPELINE: vk::Pipeline = vk::Pipeline::null();
static mut RTR_COMMAND_POOL: vk::CommandPool = vk::CommandPool::null();
static mut RTR_COMMAND_BUFFERS: [vk::CommandBuffer; RTR_MAX_SWAP_IMAGES] =
    [vk::CommandBuffer::null(); RTR_MAX_SWAP_IMAGES];
static mut RTR_IMAGE_AVAILABLE_SEMAPHORE: vk::Semaphore = vk::Semaphore::null();
static mut RTR_RENDER_FINISHED_SEMAPHORE: vk::Semaphore = vk::Semaphore::null();
static mut RTR_IN_FLIGHT_FENCE: vk::Fence = vk::Fence::null();
static mut RTR_TIMING_QUERY_POOL: vk::QueryPool = vk::QueryPool::null();
static mut RTR_MEMORY_BUFFER: vk::Buffer = vk::Buffer::null();
static mut RTR_MEMORY_BUFFER_MEMORY: vk::DeviceMemory = vk::DeviceMemory::null();
static mut RTR_MEMORY_WORDS: *mut u32 = std::ptr::null_mut();
static mut RTR_FRAME_INDEX: u32 = 0;
static mut RTR_TIMING_SUPPORTED: u32 = 0;
static mut RTR_TIMING_PENDING: u32 = 0;
static mut RTR_TIMING_IMAGE_INDEX: u32 = 0;
static mut RTR_TIMING_FRAME_INDEX: u32 = 0;
static mut RTR_TIMING_WINDOW_FIRST_FRAME: u32 = 0;
static mut RTR_TIMING_SAMPLE_COUNT: u32 = 0;
static mut RTR_TIMESTAMP_VALID_BITS: u32 = 0;
static mut RTR_TIMESTAMP_PERIOD: f32 = 1.0;
static mut RTR_TIMING_SAMPLES: [f64; RTR_TIMING_WINDOW] = [0.0; RTR_TIMING_WINDOW];
static mut RTR_START_TIME: Timespec = Timespec {
    tv_sec: 0,
    tv_nsec: 0,
};

unsafe fn instance() -> &'static vk::InstanceCommands {
    RTR_INSTANCE.as_ref().unwrap()
}

unsafe fn surface_loader() -> &'static vk::SurfaceLoader {
    RTR_SURFACE_LOADER.as_ref().unwrap()
}

unsafe fn swapchain_loader() -> &'static vk::SwapchainLoader {
    RTR_SWAPCHAIN_LOADER.as_ref().unwrap()
}

unsafe fn device() -> &'static vk::DeviceCommands {
    RTR_DEVICE.as_ref().unwrap()
}

fn rtr_error() -> vk::Result {
    vk::Result::ERROR_INITIALIZATION_FAILED
}

unsafe fn create_instance() -> VkResult<vk::MetalSurfaceLoader> {
    let entry = vk::Entry::linked();
    let app = c"realtimerays";
    let instance_exts = [
        vk::khr::surface::NAME.as_ptr(),
        vk::ext::metal_surface::NAME.as_ptr(),
        vk::khr::portability_enumeration::NAME.as_ptr(),
    ];
    let app_info = vk::ApplicationInfo {
        p_application_name: app.as_ptr(),
        application_version: vk::make_api_version(0, 0, 1, 0),
        p_engine_name: app.as_ptr(),
        engine_version: vk::make_api_version(0, 0, 1, 0),
        api_version: vk::API_VERSION_1_3,
        ..Default::default()
    };
    let create_info = vk::InstanceCreateInfo {
        flags: vk::InstanceCreateFlags::ENUMERATE_PORTABILITY_KHR,
        p_application_info: &app_info,
        enabled_extension_count: instance_exts.len() as u32,
        pp_enabled_extension_names: instance_exts.as_ptr(),
        ..Default::default()
    };

    let instance = entry.create_instance(&create_info, None)?;
    let surface_loader = vk::SurfaceLoader::new(&instance);
    let metal_surface_loader = vk::MetalSurfaceLoader::new(&instance);
    RTR_INSTANCE = Some(instance);
    RTR_SURFACE_LOADER = Some(surface_loader);

    Ok(metal_surface_loader)
}

unsafe fn create_device() -> VkResult<()> {
    let priority = 1.0f32;
    let priorities = [priority];
    let device_exts = [
        vk::khr::swapchain::NAME.as_ptr(),
        vk::khr::portability_subset::NAME.as_ptr(),
    ];
    let queue_info = vk::DeviceQueueCreateInfo {
        queue_family_index: 0,
        queue_count: priorities.len() as u32,
        p_queue_priorities: priorities.as_ptr(),
        ..Default::default()
    };
    let queue_infos = [queue_info];
    let create_info = vk::DeviceCreateInfo {
        queue_create_info_count: queue_infos.len() as u32,
        p_queue_create_infos: queue_infos.as_ptr(),
        enabled_extension_count: device_exts.len() as u32,
        pp_enabled_extension_names: device_exts.as_ptr(),
        ..Default::default()
    };

    let device = instance().create_device(RTR_PHYSICAL_DEVICE, &create_info, None)?;
    RTR_QUEUE = device.get_device_queue(0, 0);
    RTR_SWAPCHAIN_LOADER = Some(vk::SwapchainLoader::new(&device));
    RTR_DEVICE = Some(device);

    Ok(())
}

unsafe fn find_memory_type(type_bits: u32, flags: vk::MemoryPropertyFlags) -> u32 {
    let props = instance().get_physical_device_memory_properties(RTR_PHYSICAL_DEVICE);

    let mut i = 0;
    while i < props.memory_type_count {
        let memory_type = props.memory_types[i as usize];
        if (type_bits & (1u32 << i)) != 0 && memory_type.property_flags.contains(flags) {
            return i;
        }
        i += 1;
    }

    u32::MAX
}

unsafe fn create_memory_buffer() -> VkResult<()> {
    let scene = scene::build_packed_scene();
    let scene_words = scene.words.len() as u64;
    let history_words = u64::from(RTR_SWAP_EXTENT.width)
        * u64::from(RTR_SWAP_EXTENT.height)
        * RTR_HISTORY_WORDS_PER_PIXEL
        * RTR_HISTORY_PAGE_COUNT;
    let history_offset = RTR_MEMORY_HEADER_WORDS + scene_words;
    let memory_words = history_offset + history_words;
    let memory_size = memory_words * size_of::<u32>() as u64;

    let buffer_info = vk::BufferCreateInfo {
        size: memory_size,
        usage: vk::BufferUsageFlags::STORAGE_BUFFER,
        sharing_mode: vk::SharingMode::EXCLUSIVE,
        ..Default::default()
    };
    RTR_MEMORY_BUFFER = device().create_buffer(&buffer_info, None)?;

    let requirements = device().get_buffer_memory_requirements(RTR_MEMORY_BUFFER);
    let memory_type = find_memory_type(
        requirements.memory_type_bits,
        vk::MemoryPropertyFlags::HOST_VISIBLE | vk::MemoryPropertyFlags::HOST_COHERENT,
    );
    if memory_type == u32::MAX {
        return Err(rtr_error());
    }

    let alloc_info = vk::MemoryAllocateInfo {
        allocation_size: requirements.size,
        memory_type_index: memory_type,
        ..Default::default()
    };
    RTR_MEMORY_BUFFER_MEMORY = device().allocate_memory(&alloc_info, None)?;
    device().bind_buffer_memory(RTR_MEMORY_BUFFER, RTR_MEMORY_BUFFER_MEMORY, 0)?;

    let mapped = device().map_memory(
        RTR_MEMORY_BUFFER_MEMORY,
        0,
        memory_size,
        vk::MemoryMapFlags::empty(),
    )?;
    RTR_MEMORY_WORDS = mapped.cast();
    std::ptr::write_bytes(RTR_MEMORY_WORDS, 0, memory_words as usize);
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_MAGIC_WORD) = RTR_MEMORY_MAGIC;
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_VERSION_WORD) = 1;
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_WIDTH_WORD) = RTR_SWAP_EXTENT.width;
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_HEIGHT_WORD) = RTR_SWAP_EXTENT.height;
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_MOUSE_X_WORD) = (-1.0f32).to_bits();
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_MOUSE_Y_WORD) = (-1.0f32).to_bits();
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_PREV_MOUSE_X_WORD) = (-1.0f32).to_bits();
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_PREV_MOUSE_Y_WORD) = (-1.0f32).to_bits();
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_BVH_NODE_OFFSET_WORD) = RTR_MEMORY_HEADER_WORDS as u32;
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_BVH_NODE_COUNT_WORD) = scene.node_count;
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_TRIANGLE_OFFSET_WORD) = (RTR_MEMORY_HEADER_WORDS
        + scene.node_count as u64 * scene::RTR_BVH_NODE_WORDS as u64)
        as u32;
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_TRIANGLE_COUNT_WORD) = scene.triangle_count;
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_HISTORY_OFFSET_WORD) = history_offset as u32;
    std::ptr::copy_nonoverlapping(
        scene.words.as_ptr(),
        RTR_MEMORY_WORDS.add(RTR_MEMORY_HEADER_WORDS as usize),
        scene.words.len(),
    );

    Ok(())
}

unsafe fn update_memory_with(time: f32, mouse_x: f32, mouse_y: f32) {
    let prev_time_word = *RTR_MEMORY_WORDS.add(RTR_MEMORY_TIME_WORD);
    let prev_mouse_x_word = *RTR_MEMORY_WORDS.add(RTR_MEMORY_MOUSE_X_WORD);
    let prev_mouse_y_word = *RTR_MEMORY_WORDS.add(RTR_MEMORY_MOUSE_Y_WORD);

    *RTR_MEMORY_WORDS.add(RTR_MEMORY_WIDTH_WORD) = RTR_SWAP_EXTENT.width;
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_HEIGHT_WORD) = RTR_SWAP_EXTENT.height;
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_FRAME_WORD) = RTR_FRAME_INDEX;
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_MOUSE_X_WORD) = mouse_x.to_bits();
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_MOUSE_Y_WORD) = mouse_y.to_bits();
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_TIME_WORD) = time.to_bits();
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_PREV_TIME_WORD) = prev_time_word;
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_PREV_MOUSE_X_WORD) = prev_mouse_x_word;
    *RTR_MEMORY_WORDS.add(RTR_MEMORY_PREV_MOUSE_Y_WORD) = prev_mouse_y_word;
}

unsafe fn elapsed_seconds() -> f32 {
    let mut now = Timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    clock_gettime(CLOCK_MONOTONIC, &mut now);
    let seconds = (now.tv_sec - RTR_START_TIME.tv_sec) as f64;
    let nanos = (now.tv_nsec - RTR_START_TIME.tv_nsec) as f64 / 1_000_000_000.0;
    (seconds + nanos) as f32
}

unsafe fn update_memory() {
    let mut mouse_x = -1.0f32;
    let mut mouse_y = -1.0f32;
    macos::rtrWindowMouse(&mut mouse_x, &mut mouse_y);
    update_memory_with(elapsed_seconds(), mouse_x, mouse_y);
}

unsafe fn create_timing_query_pool() -> VkResult<()> {
    if RTR_TIMING_SUPPORTED == 0 {
        return Ok(());
    }

    let info = vk::QueryPoolCreateInfo {
        query_type: vk::QueryType::TIMESTAMP,
        query_count: (RTR_MAX_SWAP_IMAGES * 2) as u32,
        ..Default::default()
    };
    match device().create_query_pool(&info, None) {
        Ok(pool) => RTR_TIMING_QUERY_POOL = pool,
        Err(_) => RTR_TIMING_SUPPORTED = 0,
    }

    Ok(())
}

fn sort_timing_samples(samples: &mut [f64; RTR_TIMING_WINDOW]) {
    let mut i = 1usize;
    while i < RTR_TIMING_WINDOW {
        let value = samples[i];
        let mut j = i;
        while j > 0 && samples[j - 1] > value {
            samples[j] = samples[j - 1];
            j -= 1;
        }
        samples[j] = value;
        i += 1;
    }
}

unsafe fn push_gpu_timing(gpu_ms: f64) {
    if RTR_TIMING_SAMPLE_COUNT == 0 {
        RTR_TIMING_WINDOW_FIRST_FRAME = RTR_TIMING_FRAME_INDEX;
    }

    RTR_TIMING_SAMPLES[RTR_TIMING_SAMPLE_COUNT as usize] = gpu_ms;
    RTR_TIMING_SAMPLE_COUNT += 1;
    if RTR_TIMING_SAMPLE_COUNT < RTR_TIMING_WINDOW as u32 {
        return;
    }

    let mut sorted = [0.0f64; RTR_TIMING_WINDOW];
    let mut sum = 0.0f64;
    let mut i = 0usize;
    while i < RTR_TIMING_WINDOW {
        sorted[i] = RTR_TIMING_SAMPLES[i];
        sum += RTR_TIMING_SAMPLES[i];
        i += 1;
    }

    sort_timing_samples(&mut sorted);

    let avg = sum / RTR_TIMING_WINDOW as f64;
    let mut variance = 0.0f64;
    i = 0;
    while i < RTR_TIMING_WINDOW {
        let delta = RTR_TIMING_SAMPLES[i] - avg;
        variance += delta * delta;
        i += 1;
    }
    variance /= RTR_TIMING_WINDOW as f64;

    printf(
        b"gpu[%u] frames %u-%u %ux%u avg %.3f ms min %.3f p01 %.3f p99 %.3f max %.3f var %.6f ms^2\n\0"
            .as_ptr()
            .cast(),
        RTR_TIMING_WINDOW as u32,
        RTR_TIMING_WINDOW_FIRST_FRAME,
        RTR_TIMING_FRAME_INDEX,
        RTR_SWAP_EXTENT.width,
        RTR_SWAP_EXTENT.height,
        avg,
        sorted[0],
        sorted[0],
        sorted[RTR_TIMING_WINDOW - 2],
        sorted[RTR_TIMING_WINDOW - 1],
        variance,
    );
    fflush(__stdoutp);
    RTR_TIMING_SAMPLE_COUNT = 0;
}

unsafe fn report_gpu_timing() {
    if RTR_TIMING_SUPPORTED == 0 || RTR_TIMING_PENDING == 0 {
        return;
    }

    let mut timestamp = [0u64; 2];
    let query = RTR_TIMING_IMAGE_INDEX * 2;
    let result = device().get_query_pool_results(
        RTR_TIMING_QUERY_POOL,
        query,
        &mut timestamp,
        vk::QueryResultFlags::TYPE_64 | vk::QueryResultFlags::WAIT,
    );
    if result.is_err() {
        return;
    }

    let mut ticks = timestamp[1] - timestamp[0];
    if RTR_TIMESTAMP_VALID_BITS < 64 {
        let mask = (1u64 << RTR_TIMESTAMP_VALID_BITS) - 1;
        ticks = (timestamp[1] - timestamp[0]) & mask;
    }

    let gpu_ms = ticks as f64 * RTR_TIMESTAMP_PERIOD as f64 / 1_000_000.0;
    push_gpu_timing(gpu_ms);
    RTR_TIMING_PENDING = 0;
}

pub unsafe fn init(window_surface: *mut c_void) -> VkResult<()> {
    clock_gettime(CLOCK_MONOTONIC, &mut RTR_START_TIME);
    RTR_FRAME_INDEX = 0;
    RTR_TIMING_PENDING = 0;

    let metal_surface_loader = create_instance()?;

    let metal_info = vk::MetalSurfaceCreateInfoEXT {
        p_layer: window_surface.cast::<vk::CAMetalLayer>(),
        ..Default::default()
    };
    RTR_SURFACE = metal_surface_loader.create_metal_surface(&metal_info, None)?;

    let physical_devices = instance().enumerate_physical_devices()?;
    if physical_devices.is_empty() {
        return Err(rtr_error());
    }
    RTR_PHYSICAL_DEVICE = physical_devices[0];

    let device_props = instance().get_physical_device_properties(RTR_PHYSICAL_DEVICE);
    let queue_family_props =
        instance().get_physical_device_queue_family_properties(RTR_PHYSICAL_DEVICE);
    if queue_family_props.is_empty() {
        return Err(rtr_error());
    }
    let queue_family_props = queue_family_props[0];
    RTR_TIMESTAMP_PERIOD = device_props.limits.timestamp_period;
    RTR_TIMESTAMP_VALID_BITS = queue_family_props.timestamp_valid_bits;
    RTR_TIMING_SUPPORTED = u32::from(
        device_props.limits.timestamp_compute_and_graphics != 0
            && RTR_TIMESTAMP_VALID_BITS > 0
            && RTR_TIMESTAMP_PERIOD > 0.0,
    );

    create_device()?;

    let caps = surface_loader()
        .get_physical_device_surface_capabilities(RTR_PHYSICAL_DEVICE, RTR_SURFACE)?;
    RTR_SWAP_EXTENT = caps.current_extent;
    let mut min_image_count = RTR_MAX_SWAP_IMAGES as u32;
    if min_image_count < caps.min_image_count {
        min_image_count = caps.min_image_count;
    }
    if caps.max_image_count != 0 && min_image_count > caps.max_image_count {
        min_image_count = caps.max_image_count;
    }

    let swapchain_info = vk::SwapchainCreateInfoKHR {
        surface: RTR_SURFACE,
        min_image_count,
        image_format: vk::Format::B8G8R8A8_UNORM,
        image_color_space: vk::ColorSpaceKHR::SRGB_NONLINEAR,
        image_extent: RTR_SWAP_EXTENT,
        image_array_layers: 1,
        image_usage: vk::ImageUsageFlags::STORAGE,
        image_sharing_mode: vk::SharingMode::EXCLUSIVE,
        pre_transform: caps.current_transform,
        composite_alpha: vk::CompositeAlphaFlagsKHR::OPAQUE,
        present_mode: vk::PresentModeKHR::FIFO,
        clipped: vk::TRUE,
        ..Default::default()
    };
    RTR_SWAPCHAIN = swapchain_loader().create_swapchain(&swapchain_info, None)?;

    let swap_images = swapchain_loader().get_swapchain_images(RTR_SWAPCHAIN)?;
    let swap_image_count = swap_images.len();
    if swap_image_count < 2 || swap_image_count > RTR_MAX_SWAP_IMAGES {
        return Err(rtr_error());
    }
    let mut i = 0usize;
    while i < swap_image_count {
        RTR_SWAP_IMAGES[i] = swap_images[i];
        i += 1;
    }

    create_memory_buffer()?;
    create_timing_query_pool()?;

    let bindings = [
        vk::DescriptorSetLayoutBinding {
            binding: 0,
            descriptor_type: vk::DescriptorType::STORAGE_IMAGE,
            descriptor_count: 1,
            stage_flags: vk::ShaderStageFlags::COMPUTE,
            ..Default::default()
        },
        vk::DescriptorSetLayoutBinding {
            binding: 1,
            descriptor_type: vk::DescriptorType::STORAGE_BUFFER,
            descriptor_count: 1,
            stage_flags: vk::ShaderStageFlags::COMPUTE,
            ..Default::default()
        },
    ];
    let layout_info = vk::DescriptorSetLayoutCreateInfo {
        binding_count: bindings.len() as u32,
        p_bindings: bindings.as_ptr(),
        ..Default::default()
    };
    RTR_DESCRIPTOR_SET_LAYOUT = device().create_descriptor_set_layout(&layout_info, None)?;

    let pool_sizes = [
        vk::DescriptorPoolSize {
            ty: vk::DescriptorType::STORAGE_IMAGE,
            descriptor_count: RTR_MAX_SWAP_IMAGES as u32,
        },
        vk::DescriptorPoolSize {
            ty: vk::DescriptorType::STORAGE_BUFFER,
            descriptor_count: RTR_MAX_SWAP_IMAGES as u32,
        },
    ];
    let pool_info = vk::DescriptorPoolCreateInfo {
        max_sets: RTR_MAX_SWAP_IMAGES as u32,
        pool_size_count: pool_sizes.len() as u32,
        p_pool_sizes: pool_sizes.as_ptr(),
        ..Default::default()
    };
    RTR_DESCRIPTOR_POOL = device().create_descriptor_pool(&pool_info, None)?;

    let set_layouts = [RTR_DESCRIPTOR_SET_LAYOUT; RTR_MAX_SWAP_IMAGES];
    let set_info = vk::DescriptorSetAllocateInfo {
        descriptor_pool: RTR_DESCRIPTOR_POOL,
        descriptor_set_count: set_layouts.len() as u32,
        p_set_layouts: set_layouts.as_ptr(),
        ..Default::default()
    };
    let descriptor_sets = device().allocate_descriptor_sets(&set_info)?;
    i = 0;
    while i < RTR_MAX_SWAP_IMAGES {
        RTR_DESCRIPTOR_SETS[i] = descriptor_sets[i];
        i += 1;
    }

    let pipeline_set_layouts = [RTR_DESCRIPTOR_SET_LAYOUT];
    let pipeline_layout_info = vk::PipelineLayoutCreateInfo {
        set_layout_count: pipeline_set_layouts.len() as u32,
        p_set_layouts: pipeline_set_layouts.as_ptr(),
        ..Default::default()
    };
    RTR_PIPELINE_LAYOUT = device().create_pipeline_layout(&pipeline_layout_info, None)?;

    let shader_info = vk::ShaderModuleCreateInfo {
        code_size: size_of_val(&TRACE_COMP_SPV),
        p_code: TRACE_COMP_SPV.as_ptr(),
        ..Default::default()
    };
    let shader_module = device().create_shader_module(&shader_info, None)?;
    let stage_info = vk::PipelineShaderStageCreateInfo {
        stage: vk::ShaderStageFlags::COMPUTE,
        module: shader_module,
        p_name: c"main".as_ptr(),
        ..Default::default()
    };
    let pipeline_info = vk::ComputePipelineCreateInfo {
        stage: stage_info,
        layout: RTR_PIPELINE_LAYOUT,
        ..Default::default()
    };
    let pipeline_infos = [pipeline_info];
    let pipeline_result =
        device().create_compute_pipelines(vk::PipelineCache::null(), &pipeline_infos, None);
    device().destroy_shader_module(shader_module, None);
    match pipeline_result {
        Ok(pipelines) => RTR_PIPELINE = pipelines[0],
        Err((_pipelines, result)) => return Err(result),
    }

    let command_pool_info = vk::CommandPoolCreateInfo {
        queue_family_index: 0,
        ..Default::default()
    };
    RTR_COMMAND_POOL = device().create_command_pool(&command_pool_info, None)?;

    let command_alloc_info = vk::CommandBufferAllocateInfo {
        command_pool: RTR_COMMAND_POOL,
        level: vk::CommandBufferLevel::PRIMARY,
        command_buffer_count: swap_image_count as u32,
        ..Default::default()
    };
    let command_buffers = device().allocate_command_buffers(&command_alloc_info)?;
    i = 0;
    while i < swap_image_count {
        RTR_COMMAND_BUFFERS[i] = command_buffers[i];
        i += 1;
    }

    let image_range = vk::ImageSubresourceRange {
        aspect_mask: vk::ImageAspectFlags::COLOR,
        level_count: 1,
        layer_count: 1,
        ..Default::default()
    };

    i = 0;
    while i < swap_image_count {
        let image_view_info = vk::ImageViewCreateInfo {
            image: RTR_SWAP_IMAGES[i],
            view_type: vk::ImageViewType::TYPE_2D,
            format: vk::Format::B8G8R8A8_UNORM,
            components: vk::ComponentMapping::default(),
            subresource_range: image_range,
            ..Default::default()
        };
        RTR_SWAP_IMAGE_VIEWS[i] = device().create_image_view(&image_view_info, None)?;

        let image_infos = [vk::DescriptorImageInfo {
            image_view: RTR_SWAP_IMAGE_VIEWS[i],
            image_layout: vk::ImageLayout::GENERAL,
            ..Default::default()
        }];
        let buffer_infos = [vk::DescriptorBufferInfo {
            buffer: RTR_MEMORY_BUFFER,
            range: vk::WHOLE_SIZE,
            ..Default::default()
        }];
        let writes = [
            vk::WriteDescriptorSet {
                dst_set: RTR_DESCRIPTOR_SETS[i],
                dst_binding: 0,
                descriptor_count: image_infos.len() as u32,
                descriptor_type: vk::DescriptorType::STORAGE_IMAGE,
                p_image_info: image_infos.as_ptr(),
                ..Default::default()
            },
            vk::WriteDescriptorSet {
                dst_set: RTR_DESCRIPTOR_SETS[i],
                dst_binding: 1,
                descriptor_count: buffer_infos.len() as u32,
                descriptor_type: vk::DescriptorType::STORAGE_BUFFER,
                p_buffer_info: buffer_infos.as_ptr(),
                ..Default::default()
            },
        ];
        device().update_descriptor_sets(&writes, &[]);

        let begin_info = vk::CommandBufferBeginInfo::default();
        device().begin_command_buffer(RTR_COMMAND_BUFFERS[i], &begin_info)?;

        if RTR_TIMING_SUPPORTED != 0 {
            device().cmd_reset_query_pool(
                RTR_COMMAND_BUFFERS[i],
                RTR_TIMING_QUERY_POOL,
                (i as u32) * 2,
                2,
            );
        }

        let to_general = [vk::ImageMemoryBarrier {
            dst_access_mask: vk::AccessFlags::SHADER_WRITE,
            old_layout: vk::ImageLayout::UNDEFINED,
            new_layout: vk::ImageLayout::GENERAL,
            src_queue_family_index: vk::QUEUE_FAMILY_IGNORED,
            dst_queue_family_index: vk::QUEUE_FAMILY_IGNORED,
            image: RTR_SWAP_IMAGES[i],
            subresource_range: image_range,
            ..Default::default()
        }];
        device().cmd_pipeline_barrier(
            RTR_COMMAND_BUFFERS[i],
            vk::PipelineStageFlags::TOP_OF_PIPE,
            vk::PipelineStageFlags::COMPUTE_SHADER,
            vk::DependencyFlags::empty(),
            &[],
            &[],
            &to_general,
        );

        if RTR_TIMING_SUPPORTED != 0 {
            device().cmd_write_timestamp(
                RTR_COMMAND_BUFFERS[i],
                vk::PipelineStageFlags::COMPUTE_SHADER,
                RTR_TIMING_QUERY_POOL,
                (i as u32) * 2,
            );
        }

        let memory_barrier = [vk::BufferMemoryBarrier {
            src_access_mask: vk::AccessFlags::HOST_WRITE | vk::AccessFlags::SHADER_WRITE,
            dst_access_mask: vk::AccessFlags::SHADER_READ | vk::AccessFlags::SHADER_WRITE,
            src_queue_family_index: vk::QUEUE_FAMILY_IGNORED,
            dst_queue_family_index: vk::QUEUE_FAMILY_IGNORED,
            buffer: RTR_MEMORY_BUFFER,
            size: vk::WHOLE_SIZE,
            ..Default::default()
        }];
        device().cmd_pipeline_barrier(
            RTR_COMMAND_BUFFERS[i],
            vk::PipelineStageFlags::HOST | vk::PipelineStageFlags::COMPUTE_SHADER,
            vk::PipelineStageFlags::COMPUTE_SHADER,
            vk::DependencyFlags::empty(),
            &[],
            &memory_barrier,
            &[],
        );

        device().cmd_bind_pipeline(
            RTR_COMMAND_BUFFERS[i],
            vk::PipelineBindPoint::COMPUTE,
            RTR_PIPELINE,
        );
        device().cmd_bind_descriptor_sets(
            RTR_COMMAND_BUFFERS[i],
            vk::PipelineBindPoint::COMPUTE,
            RTR_PIPELINE_LAYOUT,
            0,
            &[RTR_DESCRIPTOR_SETS[i]],
            &[],
        );
        device().cmd_dispatch(
            RTR_COMMAND_BUFFERS[i],
            (RTR_SWAP_EXTENT.width + RTR_TILE_SIZE - 1) / RTR_TILE_SIZE,
            (RTR_SWAP_EXTENT.height + RTR_TILE_SIZE - 1) / RTR_TILE_SIZE,
            1,
        );

        if RTR_TIMING_SUPPORTED != 0 {
            device().cmd_write_timestamp(
                RTR_COMMAND_BUFFERS[i],
                vk::PipelineStageFlags::COMPUTE_SHADER,
                RTR_TIMING_QUERY_POOL,
                (i as u32) * 2 + 1,
            );
        }

        let to_present = [vk::ImageMemoryBarrier {
            src_access_mask: vk::AccessFlags::SHADER_WRITE,
            old_layout: vk::ImageLayout::GENERAL,
            new_layout: vk::ImageLayout::PRESENT_SRC_KHR,
            src_queue_family_index: vk::QUEUE_FAMILY_IGNORED,
            dst_queue_family_index: vk::QUEUE_FAMILY_IGNORED,
            image: RTR_SWAP_IMAGES[i],
            subresource_range: image_range,
            ..Default::default()
        }];
        device().cmd_pipeline_barrier(
            RTR_COMMAND_BUFFERS[i],
            vk::PipelineStageFlags::COMPUTE_SHADER,
            vk::PipelineStageFlags::BOTTOM_OF_PIPE,
            vk::DependencyFlags::empty(),
            &[],
            &[],
            &to_present,
        );

        device().end_command_buffer(RTR_COMMAND_BUFFERS[i])?;
        i += 1;
    }

    let semaphore_info = vk::SemaphoreCreateInfo::default();
    RTR_IMAGE_AVAILABLE_SEMAPHORE = device().create_semaphore(&semaphore_info, None)?;
    RTR_RENDER_FINISHED_SEMAPHORE = device().create_semaphore(&semaphore_info, None)?;

    let fence_info = vk::FenceCreateInfo {
        flags: vk::FenceCreateFlags::SIGNALED,
        ..Default::default()
    };
    RTR_IN_FLIGHT_FENCE = device().create_fence(&fence_info, None)?;

    Ok(())
}

pub unsafe fn frame() {
    let fences = [RTR_IN_FLIGHT_FENCE];
    let _ = device().wait_for_fences(&fences, true, u64::MAX);
    report_gpu_timing();
    update_memory();
    let _ = device().reset_fences(&fences);

    let Ok((image_index, _suboptimal)) = swapchain_loader().acquire_next_image(
        RTR_SWAPCHAIN,
        u64::MAX,
        RTR_IMAGE_AVAILABLE_SEMAPHORE,
        vk::Fence::null(),
    ) else {
        return;
    };

    let wait_stages = [vk::PipelineStageFlags::COMPUTE_SHADER];
    let wait_semaphores = [RTR_IMAGE_AVAILABLE_SEMAPHORE];
    let command_buffers = [RTR_COMMAND_BUFFERS[image_index as usize]];
    let signal_semaphores = [RTR_RENDER_FINISHED_SEMAPHORE];
    let submit = vk::SubmitInfo {
        wait_semaphore_count: wait_semaphores.len() as u32,
        p_wait_semaphores: wait_semaphores.as_ptr(),
        p_wait_dst_stage_mask: wait_stages.as_ptr(),
        command_buffer_count: command_buffers.len() as u32,
        p_command_buffers: command_buffers.as_ptr(),
        signal_semaphore_count: signal_semaphores.len() as u32,
        p_signal_semaphores: signal_semaphores.as_ptr(),
        ..Default::default()
    };
    let submits = [submit];
    let _ = device().queue_submit(RTR_QUEUE, &submits, RTR_IN_FLIGHT_FENCE);

    let swapchains = [RTR_SWAPCHAIN];
    let image_indices = [image_index];
    let present = vk::PresentInfoKHR {
        wait_semaphore_count: signal_semaphores.len() as u32,
        p_wait_semaphores: signal_semaphores.as_ptr(),
        swapchain_count: swapchains.len() as u32,
        p_swapchains: swapchains.as_ptr(),
        p_image_indices: image_indices.as_ptr(),
        ..Default::default()
    };
    let _ = swapchain_loader().queue_present(RTR_QUEUE, &present);

    if RTR_TIMING_SUPPORTED != 0 {
        RTR_TIMING_PENDING = 1;
        RTR_TIMING_IMAGE_INDEX = image_index;
        RTR_TIMING_FRAME_INDEX = RTR_FRAME_INDEX;
    }

    RTR_FRAME_INDEX += 1;
}

#[derive(Clone, Copy)]
struct SampleStats {
    avg: f64,
    min: f64,
    p50: f64,
    p95: f64,
    max: f64,
}

fn duration_ms(start: Instant) -> f64 {
    start.elapsed().as_secs_f64() * 1000.0
}

fn sample_stats(samples: &[f64]) -> Option<SampleStats> {
    if samples.is_empty() {
        return None;
    }

    let mut sorted = samples.to_vec();
    sorted.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));

    let sum: f64 = samples.iter().sum();
    let mid = sorted.len() / 2;
    let p50 = if sorted.len() % 2 == 0 {
        (sorted[mid - 1] + sorted[mid]) * 0.5
    } else {
        sorted[mid]
    };
    let p95_rank = ((95 * sorted.len() + 99) / 100).saturating_sub(1);
    let p95 = sorted[p95_rank.min(sorted.len() - 1)];

    Some(SampleStats {
        avg: sum / samples.len() as f64,
        min: sorted[0],
        p50,
        p95,
        max: sorted[sorted.len() - 1],
    })
}

fn eprint_sample_stats(label: &str, samples: &[f64]) {
    if let Some(stats) = sample_stats(samples) {
        eprintln!(
            "xpost {label}: avg {:.3} ms min {:.3} p50 {:.3} p95 {:.3} max {:.3}",
            stats.avg, stats.min, stats.p50, stats.p95, stats.max
        );
    }
}

unsafe fn physical_device_name(props: &vk::PhysicalDeviceProperties) -> String {
    CStr::from_ptr(props.device_name.as_ptr())
        .to_string_lossy()
        .into_owned()
}

unsafe fn collect_gpu_sample(samples: &mut Vec<f64>) {
    if RTR_TIMING_SUPPORTED == 0 {
        return;
    }

    let mut timestamp = [0u64; 2];
    if device()
        .get_query_pool_results(
            RTR_TIMING_QUERY_POOL,
            0,
            &mut timestamp,
            vk::QueryResultFlags::TYPE_64 | vk::QueryResultFlags::WAIT,
        )
        .is_err()
    {
        return;
    }

    let mut ticks = timestamp[1] - timestamp[0];
    if RTR_TIMESTAMP_VALID_BITS < 64 {
        let mask = (1u64 << RTR_TIMESTAMP_VALID_BITS) - 1;
        ticks = (timestamp[1] - timestamp[0]) & mask;
    }

    samples.push(ticks as f64 * RTR_TIMESTAMP_PERIOD as f64 / 1_000_000.0);
}

unsafe fn record_export_frame(
    command_buffer: vk::CommandBuffer,
    image: vk::Image,
    staging_buffer: vk::Buffer,
    old_layout: vk::ImageLayout,
) -> VkResult<()> {
    let image_range = vk::ImageSubresourceRange {
        aspect_mask: vk::ImageAspectFlags::COLOR,
        level_count: 1,
        layer_count: 1,
        ..Default::default()
    };

    device().begin_command_buffer(command_buffer, &vk::CommandBufferBeginInfo::default())?;

    if RTR_TIMING_SUPPORTED != 0 {
        device().cmd_reset_query_pool(command_buffer, RTR_TIMING_QUERY_POOL, 0, 2);
    }

    let (src_stage, src_access) = if old_layout == vk::ImageLayout::UNDEFINED {
        (
            vk::PipelineStageFlags::TOP_OF_PIPE,
            vk::AccessFlags::empty(),
        )
    } else {
        (
            vk::PipelineStageFlags::TRANSFER,
            vk::AccessFlags::TRANSFER_READ,
        )
    };
    let to_general = [vk::ImageMemoryBarrier {
        src_access_mask: src_access,
        dst_access_mask: vk::AccessFlags::SHADER_WRITE,
        old_layout,
        new_layout: vk::ImageLayout::GENERAL,
        src_queue_family_index: vk::QUEUE_FAMILY_IGNORED,
        dst_queue_family_index: vk::QUEUE_FAMILY_IGNORED,
        image,
        subresource_range: image_range,
        ..Default::default()
    }];
    device().cmd_pipeline_barrier(
        command_buffer,
        src_stage,
        vk::PipelineStageFlags::COMPUTE_SHADER,
        vk::DependencyFlags::empty(),
        &[],
        &[],
        &to_general,
    );

    let memory_barrier = [vk::BufferMemoryBarrier {
        src_access_mask: vk::AccessFlags::HOST_WRITE | vk::AccessFlags::SHADER_WRITE,
        dst_access_mask: vk::AccessFlags::SHADER_READ | vk::AccessFlags::SHADER_WRITE,
        src_queue_family_index: vk::QUEUE_FAMILY_IGNORED,
        dst_queue_family_index: vk::QUEUE_FAMILY_IGNORED,
        buffer: RTR_MEMORY_BUFFER,
        size: vk::WHOLE_SIZE,
        ..Default::default()
    }];
    device().cmd_pipeline_barrier(
        command_buffer,
        vk::PipelineStageFlags::HOST | vk::PipelineStageFlags::COMPUTE_SHADER,
        vk::PipelineStageFlags::COMPUTE_SHADER,
        vk::DependencyFlags::empty(),
        &[],
        &memory_barrier,
        &[],
    );

    if RTR_TIMING_SUPPORTED != 0 {
        device().cmd_write_timestamp(
            command_buffer,
            vk::PipelineStageFlags::COMPUTE_SHADER,
            RTR_TIMING_QUERY_POOL,
            0,
        );
    }

    device().cmd_bind_pipeline(command_buffer, vk::PipelineBindPoint::COMPUTE, RTR_PIPELINE);
    device().cmd_bind_descriptor_sets(
        command_buffer,
        vk::PipelineBindPoint::COMPUTE,
        RTR_PIPELINE_LAYOUT,
        0,
        &[RTR_DESCRIPTOR_SETS[0]],
        &[],
    );
    device().cmd_dispatch(
        command_buffer,
        (RTR_SWAP_EXTENT.width + RTR_TILE_SIZE - 1) / RTR_TILE_SIZE,
        (RTR_SWAP_EXTENT.height + RTR_TILE_SIZE - 1) / RTR_TILE_SIZE,
        1,
    );

    if RTR_TIMING_SUPPORTED != 0 {
        device().cmd_write_timestamp(
            command_buffer,
            vk::PipelineStageFlags::COMPUTE_SHADER,
            RTR_TIMING_QUERY_POOL,
            1,
        );
    }

    let to_transfer = [vk::ImageMemoryBarrier {
        src_access_mask: vk::AccessFlags::SHADER_WRITE,
        dst_access_mask: vk::AccessFlags::TRANSFER_READ,
        old_layout: vk::ImageLayout::GENERAL,
        new_layout: vk::ImageLayout::TRANSFER_SRC_OPTIMAL,
        src_queue_family_index: vk::QUEUE_FAMILY_IGNORED,
        dst_queue_family_index: vk::QUEUE_FAMILY_IGNORED,
        image,
        subresource_range: image_range,
        ..Default::default()
    }];
    device().cmd_pipeline_barrier(
        command_buffer,
        vk::PipelineStageFlags::COMPUTE_SHADER,
        vk::PipelineStageFlags::TRANSFER,
        vk::DependencyFlags::empty(),
        &[],
        &[],
        &to_transfer,
    );

    let copy = [vk::BufferImageCopy {
        image_subresource: vk::ImageSubresourceLayers {
            aspect_mask: vk::ImageAspectFlags::COLOR,
            layer_count: 1,
            ..Default::default()
        },
        image_extent: vk::Extent3D {
            width: RTR_SWAP_EXTENT.width,
            height: RTR_SWAP_EXTENT.height,
            depth: 1,
        },
        ..Default::default()
    }];
    device().cmd_copy_image_to_buffer(
        command_buffer,
        image,
        vk::ImageLayout::TRANSFER_SRC_OPTIMAL,
        staging_buffer,
        &copy,
    );

    device().end_command_buffer(command_buffer)
}

pub unsafe fn write_frames<W: Write>(
    out: &mut W,
    width: u32,
    height: u32,
    frames: u32,
    fps: u32,
) -> VkResult<()> {
    if width == 0 || height == 0 || frames == 0 || fps == 0 {
        return Err(rtr_error());
    }

    let total_start = Instant::now();
    clock_gettime(CLOCK_MONOTONIC, &mut RTR_START_TIME);
    RTR_SWAP_EXTENT = vk::Extent2D { width, height };
    RTR_FRAME_INDEX = 0;
    RTR_TIMING_PENDING = 0;
    RTR_TIMING_SAMPLE_COUNT = 0;

    let _metal_surface_loader = create_instance()?;

    let physical_devices = instance().enumerate_physical_devices()?;
    if physical_devices.is_empty() {
        return Err(rtr_error());
    }
    RTR_PHYSICAL_DEVICE = physical_devices[0];

    let device_props = instance().get_physical_device_properties(RTR_PHYSICAL_DEVICE);
    let queue_family_props =
        instance().get_physical_device_queue_family_properties(RTR_PHYSICAL_DEVICE);
    if queue_family_props.is_empty() {
        return Err(rtr_error());
    }
    let queue_family_props = queue_family_props[0];
    RTR_TIMESTAMP_PERIOD = device_props.limits.timestamp_period;
    RTR_TIMESTAMP_VALID_BITS = queue_family_props.timestamp_valid_bits;
    RTR_TIMING_SUPPORTED = u32::from(
        device_props.limits.timestamp_compute_and_graphics != 0
            && RTR_TIMESTAMP_VALID_BITS > 0
            && RTR_TIMESTAMP_PERIOD > 0.0,
    );
    let device_name = physical_device_name(&device_props);

    create_device()?;
    create_memory_buffer()?;
    create_timing_query_pool()?;

    let image_bytes = u64::from(width) * u64::from(height) * 4;
    let image_info = vk::ImageCreateInfo {
        image_type: vk::ImageType::TYPE_2D,
        format: vk::Format::R8G8B8A8_UNORM,
        extent: vk::Extent3D {
            width,
            height,
            depth: 1,
        },
        mip_levels: 1,
        array_layers: 1,
        samples: vk::SampleCountFlags::TYPE_1,
        tiling: vk::ImageTiling::OPTIMAL,
        usage: vk::ImageUsageFlags::STORAGE | vk::ImageUsageFlags::TRANSFER_SRC,
        sharing_mode: vk::SharingMode::EXCLUSIVE,
        initial_layout: vk::ImageLayout::UNDEFINED,
        ..Default::default()
    };
    let image = device().create_image(&image_info, None)?;

    let image_req = device().get_image_memory_requirements(image);
    let image_memory_type = find_memory_type(
        image_req.memory_type_bits,
        vk::MemoryPropertyFlags::DEVICE_LOCAL,
    );
    if image_memory_type == u32::MAX {
        return Err(rtr_error());
    }
    let image_alloc = vk::MemoryAllocateInfo {
        allocation_size: image_req.size,
        memory_type_index: image_memory_type,
        ..Default::default()
    };
    let image_memory = device().allocate_memory(&image_alloc, None)?;
    device().bind_image_memory(image, image_memory, 0)?;

    let image_view_info = vk::ImageViewCreateInfo {
        image,
        view_type: vk::ImageViewType::TYPE_2D,
        format: vk::Format::R8G8B8A8_UNORM,
        subresource_range: vk::ImageSubresourceRange {
            aspect_mask: vk::ImageAspectFlags::COLOR,
            level_count: 1,
            layer_count: 1,
            ..Default::default()
        },
        ..Default::default()
    };
    let image_view = device().create_image_view(&image_view_info, None)?;

    let staging_info = vk::BufferCreateInfo {
        size: image_bytes,
        usage: vk::BufferUsageFlags::TRANSFER_DST,
        sharing_mode: vk::SharingMode::EXCLUSIVE,
        ..Default::default()
    };
    let staging_buffer = device().create_buffer(&staging_info, None)?;
    let staging_req = device().get_buffer_memory_requirements(staging_buffer);
    let staging_memory_type = find_memory_type(
        staging_req.memory_type_bits,
        vk::MemoryPropertyFlags::HOST_VISIBLE | vk::MemoryPropertyFlags::HOST_COHERENT,
    );
    if staging_memory_type == u32::MAX {
        return Err(rtr_error());
    }
    let staging_alloc = vk::MemoryAllocateInfo {
        allocation_size: staging_req.size,
        memory_type_index: staging_memory_type,
        ..Default::default()
    };
    let staging_memory = device().allocate_memory(&staging_alloc, None)?;
    device().bind_buffer_memory(staging_buffer, staging_memory, 0)?;
    let staging_pixels = device()
        .map_memory(staging_memory, 0, image_bytes, vk::MemoryMapFlags::empty())?
        .cast::<u8>();

    let bindings = [
        vk::DescriptorSetLayoutBinding {
            binding: 0,
            descriptor_type: vk::DescriptorType::STORAGE_IMAGE,
            descriptor_count: 1,
            stage_flags: vk::ShaderStageFlags::COMPUTE,
            ..Default::default()
        },
        vk::DescriptorSetLayoutBinding {
            binding: 1,
            descriptor_type: vk::DescriptorType::STORAGE_BUFFER,
            descriptor_count: 1,
            stage_flags: vk::ShaderStageFlags::COMPUTE,
            ..Default::default()
        },
    ];
    let layout_info = vk::DescriptorSetLayoutCreateInfo {
        binding_count: bindings.len() as u32,
        p_bindings: bindings.as_ptr(),
        ..Default::default()
    };
    RTR_DESCRIPTOR_SET_LAYOUT = device().create_descriptor_set_layout(&layout_info, None)?;

    let pool_sizes = [
        vk::DescriptorPoolSize {
            ty: vk::DescriptorType::STORAGE_IMAGE,
            descriptor_count: 1,
        },
        vk::DescriptorPoolSize {
            ty: vk::DescriptorType::STORAGE_BUFFER,
            descriptor_count: 1,
        },
    ];
    let pool_info = vk::DescriptorPoolCreateInfo {
        max_sets: 1,
        pool_size_count: pool_sizes.len() as u32,
        p_pool_sizes: pool_sizes.as_ptr(),
        ..Default::default()
    };
    RTR_DESCRIPTOR_POOL = device().create_descriptor_pool(&pool_info, None)?;

    let set_layouts = [RTR_DESCRIPTOR_SET_LAYOUT];
    let set_info = vk::DescriptorSetAllocateInfo {
        descriptor_pool: RTR_DESCRIPTOR_POOL,
        descriptor_set_count: set_layouts.len() as u32,
        p_set_layouts: set_layouts.as_ptr(),
        ..Default::default()
    };
    let descriptor_sets = device().allocate_descriptor_sets(&set_info)?;
    RTR_DESCRIPTOR_SETS[0] = descriptor_sets[0];

    let image_infos = [vk::DescriptorImageInfo {
        image_view,
        image_layout: vk::ImageLayout::GENERAL,
        ..Default::default()
    }];
    let buffer_infos = [vk::DescriptorBufferInfo {
        buffer: RTR_MEMORY_BUFFER,
        range: vk::WHOLE_SIZE,
        ..Default::default()
    }];
    let writes = [
        vk::WriteDescriptorSet {
            dst_set: RTR_DESCRIPTOR_SETS[0],
            dst_binding: 0,
            descriptor_count: image_infos.len() as u32,
            descriptor_type: vk::DescriptorType::STORAGE_IMAGE,
            p_image_info: image_infos.as_ptr(),
            ..Default::default()
        },
        vk::WriteDescriptorSet {
            dst_set: RTR_DESCRIPTOR_SETS[0],
            dst_binding: 1,
            descriptor_count: buffer_infos.len() as u32,
            descriptor_type: vk::DescriptorType::STORAGE_BUFFER,
            p_buffer_info: buffer_infos.as_ptr(),
            ..Default::default()
        },
    ];
    device().update_descriptor_sets(&writes, &[]);

    let pipeline_set_layouts = [RTR_DESCRIPTOR_SET_LAYOUT];
    let pipeline_layout_info = vk::PipelineLayoutCreateInfo {
        set_layout_count: pipeline_set_layouts.len() as u32,
        p_set_layouts: pipeline_set_layouts.as_ptr(),
        ..Default::default()
    };
    RTR_PIPELINE_LAYOUT = device().create_pipeline_layout(&pipeline_layout_info, None)?;

    let shader_info = vk::ShaderModuleCreateInfo {
        code_size: size_of_val(&TRACE_COMP_SPV),
        p_code: TRACE_COMP_SPV.as_ptr(),
        ..Default::default()
    };
    let shader_module = device().create_shader_module(&shader_info, None)?;
    let stage_info = vk::PipelineShaderStageCreateInfo {
        stage: vk::ShaderStageFlags::COMPUTE,
        module: shader_module,
        p_name: c"main".as_ptr(),
        ..Default::default()
    };
    let pipeline_info = vk::ComputePipelineCreateInfo {
        stage: stage_info,
        layout: RTR_PIPELINE_LAYOUT,
        ..Default::default()
    };
    let pipeline_infos = [pipeline_info];
    let pipeline_result =
        device().create_compute_pipelines(vk::PipelineCache::null(), &pipeline_infos, None);
    device().destroy_shader_module(shader_module, None);
    match pipeline_result {
        Ok(pipelines) => RTR_PIPELINE = pipelines[0],
        Err((_pipelines, result)) => return Err(result),
    }

    let command_pool_info = vk::CommandPoolCreateInfo {
        flags: vk::CommandPoolCreateFlags::RESET_COMMAND_BUFFER,
        queue_family_index: 0,
        ..Default::default()
    };
    RTR_COMMAND_POOL = device().create_command_pool(&command_pool_info, None)?;

    let command_alloc_info = vk::CommandBufferAllocateInfo {
        command_pool: RTR_COMMAND_POOL,
        level: vk::CommandBufferLevel::PRIMARY,
        command_buffer_count: 1,
        ..Default::default()
    };
    let command_buffers = device().allocate_command_buffers(&command_alloc_info)?;
    RTR_COMMAND_BUFFERS[0] = command_buffers[0];

    let fence = device().create_fence(&vk::FenceCreateInfo::default(), None)?;
    let setup_ms = duration_ms(total_start);
    let seconds = frames as f64 / fps as f64;
    eprintln!(
        "xpost render: {width}x{height} {frames} frames @ {fps} fps ({seconds:.2}s) on {device_name}"
    );
    eprintln!(
        "xpost gpu_timestamps: {}",
        if RTR_TIMING_SUPPORTED != 0 {
            "enabled"
        } else {
            "unavailable"
        }
    );

    let render_start = Instant::now();
    let mut old_layout = vk::ImageLayout::UNDEFINED;
    let mut bytes_written = 0u64;
    let mut gpu_samples = Vec::with_capacity(frames as usize);
    let mut submit_wait_samples = Vec::with_capacity(frames as usize);
    let mut write_samples = Vec::with_capacity(frames as usize);
    let mut total_frame_samples = Vec::with_capacity(frames as usize);
    let progress_interval = if frames >= 300 { 100 } else { frames.max(1) };

    for frame in 0..frames {
        let frame_start = Instant::now();
        update_memory_with(frame as f32 / fps as f32, -1.0, -1.0);
        device()
            .reset_command_buffer(RTR_COMMAND_BUFFERS[0], vk::CommandBufferResetFlags::empty())?;
        record_export_frame(RTR_COMMAND_BUFFERS[0], image, staging_buffer, old_layout)?;
        old_layout = vk::ImageLayout::TRANSFER_SRC_OPTIMAL;

        let command_buffers = [RTR_COMMAND_BUFFERS[0]];
        let submit = vk::SubmitInfo {
            command_buffer_count: command_buffers.len() as u32,
            p_command_buffers: command_buffers.as_ptr(),
            ..Default::default()
        };
        device().queue_submit(RTR_QUEUE, &[submit], fence)?;
        device().wait_for_fences(&[fence], true, u64::MAX)?;
        device().reset_fences(&[fence])?;
        submit_wait_samples.push(duration_ms(frame_start));
        collect_gpu_sample(&mut gpu_samples);

        let write_start = Instant::now();
        let pixels = std::slice::from_raw_parts(staging_pixels, image_bytes as usize);
        out.write_all(pixels).map_err(|_| rtr_error())?;
        bytes_written += image_bytes;
        write_samples.push(duration_ms(write_start));
        total_frame_samples.push(duration_ms(frame_start));

        RTR_FRAME_INDEX += 1;
        let done = frame + 1;
        if done == frames || done % progress_interval == 0 {
            eprintln!("xpost progress: {done}/{frames} frames");
        }
    }

    out.flush().map_err(|_| rtr_error())?;

    let render_wall_ms = duration_ms(render_start);
    let total_wall_ms = duration_ms(total_start);
    let effective_fps = frames as f64 / (render_wall_ms / 1000.0);
    eprintln!(
        "xpost summary: raw rgba bytes {bytes_written} setup {:.3} ms render {:.3} s total {:.3} s effective {:.2} fps",
        setup_ms,
        render_wall_ms / 1000.0,
        total_wall_ms / 1000.0,
        effective_fps
    );
    eprint_sample_stats("cpu_submit_wait", &submit_wait_samples);
    eprint_sample_stats("stdout_write", &write_samples);
    eprint_sample_stats("frame_total", &total_frame_samples);
    eprint_sample_stats("gpu_compute", &gpu_samples);

    Ok(())
}
