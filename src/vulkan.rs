#![allow(static_mut_refs)]

use crate::macos;
use crate::vk::{self, VkResult};
use std::ffi::{c_char, c_int, c_void};
use std::mem::{size_of, size_of_val};

include!(concat!(env!("OUT_DIR"), "/trace_comp_spv.rs"));

const RTR_MAX_SWAP_IMAGES: usize = 3;
const RTR_TILE_SIZE: u32 = 8;
const RTR_MEMORY_HEADER_WORDS: u64 = 16;
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
    let history_words = u64::from(RTR_SWAP_EXTENT.width)
        * u64::from(RTR_SWAP_EXTENT.height)
        * RTR_HISTORY_WORDS_PER_PIXEL
        * RTR_HISTORY_PAGE_COUNT;
    let memory_words = RTR_MEMORY_HEADER_WORDS + history_words;
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
