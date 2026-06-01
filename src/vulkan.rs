#![allow(static_mut_refs)]

use crate::macos;
use ash::prelude::VkResult;
use ash::vk;
use std::ffi::{c_char, c_int, c_void};
use std::mem::size_of;

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

static mut RTR_INSTANCE: Option<ash::Instance> = None;
static mut RTR_SURFACE_LOADER: Option<ash::khr::surface::Instance> = None;
static mut RTR_SWAPCHAIN_LOADER: Option<ash::khr::swapchain::Device> = None;
static mut RTR_SURFACE: vk::SurfaceKHR = vk::SurfaceKHR::null();
static mut RTR_PHYSICAL_DEVICE: vk::PhysicalDevice = vk::PhysicalDevice::null();
static mut RTR_DEVICE: Option<ash::Device> = None;
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

unsafe fn instance() -> &'static ash::Instance {
    RTR_INSTANCE.as_ref().unwrap()
}

unsafe fn surface_loader() -> &'static ash::khr::surface::Instance {
    RTR_SURFACE_LOADER.as_ref().unwrap()
}

unsafe fn swapchain_loader() -> &'static ash::khr::swapchain::Device {
    RTR_SWAPCHAIN_LOADER.as_ref().unwrap()
}

unsafe fn device() -> &'static ash::Device {
    RTR_DEVICE.as_ref().unwrap()
}

fn rtr_error() -> vk::Result {
    vk::Result::ERROR_INITIALIZATION_FAILED
}

unsafe fn create_instance() -> VkResult<ash::ext::metal_surface::Instance> {
    let entry = ash::Entry::linked();
    let app = c"realtimerays";
    let instance_exts = [
        ash::khr::surface::NAME.as_ptr(),
        ash::ext::metal_surface::NAME.as_ptr(),
        ash::khr::portability_enumeration::NAME.as_ptr(),
    ];
    let app_info = vk::ApplicationInfo::default()
        .application_name(app)
        .application_version(vk::make_api_version(0, 0, 1, 0))
        .engine_name(app)
        .engine_version(vk::make_api_version(0, 0, 1, 0))
        .api_version(vk::API_VERSION_1_3);
    let create_info = vk::InstanceCreateInfo::default()
        .flags(vk::InstanceCreateFlags::ENUMERATE_PORTABILITY_KHR)
        .application_info(&app_info)
        .enabled_extension_names(&instance_exts);

    let instance = entry.create_instance(&create_info, None)?;
    let surface_loader = ash::khr::surface::Instance::new(&entry, &instance);
    let metal_surface_loader = ash::ext::metal_surface::Instance::new(&entry, &instance);
    RTR_INSTANCE = Some(instance);
    RTR_SURFACE_LOADER = Some(surface_loader);

    Ok(metal_surface_loader)
}

unsafe fn create_device() -> VkResult<()> {
    let priority = 1.0f32;
    let priorities = [priority];
    let device_exts = [
        ash::khr::swapchain::NAME.as_ptr(),
        ash::khr::portability_subset::NAME.as_ptr(),
    ];
    let queue_info = vk::DeviceQueueCreateInfo::default()
        .queue_family_index(0)
        .queue_priorities(&priorities);
    let queue_infos = [queue_info];
    let create_info = vk::DeviceCreateInfo::default()
        .queue_create_infos(&queue_infos)
        .enabled_extension_names(&device_exts);

    let device = instance().create_device(RTR_PHYSICAL_DEVICE, &create_info, None)?;
    RTR_QUEUE = device.get_device_queue(0, 0);
    RTR_SWAPCHAIN_LOADER = Some(ash::khr::swapchain::Device::new(instance(), &device));
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

    let buffer_info = vk::BufferCreateInfo::default()
        .size(memory_size)
        .usage(vk::BufferUsageFlags::STORAGE_BUFFER)
        .sharing_mode(vk::SharingMode::EXCLUSIVE);
    RTR_MEMORY_BUFFER = device().create_buffer(&buffer_info, None)?;

    let requirements = device().get_buffer_memory_requirements(RTR_MEMORY_BUFFER);
    let memory_type = find_memory_type(
        requirements.memory_type_bits,
        vk::MemoryPropertyFlags::HOST_VISIBLE | vk::MemoryPropertyFlags::HOST_COHERENT,
    );
    if memory_type == u32::MAX {
        return Err(rtr_error());
    }

    let alloc_info = vk::MemoryAllocateInfo::default()
        .allocation_size(requirements.size)
        .memory_type_index(memory_type);
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

    let info = vk::QueryPoolCreateInfo::default()
        .query_type(vk::QueryType::TIMESTAMP)
        .query_count((RTR_MAX_SWAP_IMAGES * 2) as u32);
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

    let metal_info =
        vk::MetalSurfaceCreateInfoEXT::default().layer(window_surface.cast::<vk::CAMetalLayer>());
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

    let swapchain_info = vk::SwapchainCreateInfoKHR::default()
        .surface(RTR_SURFACE)
        .min_image_count(min_image_count)
        .image_format(vk::Format::B8G8R8A8_UNORM)
        .image_color_space(vk::ColorSpaceKHR::SRGB_NONLINEAR)
        .image_extent(RTR_SWAP_EXTENT)
        .image_array_layers(1)
        .image_usage(vk::ImageUsageFlags::STORAGE)
        .image_sharing_mode(vk::SharingMode::EXCLUSIVE)
        .pre_transform(caps.current_transform)
        .composite_alpha(vk::CompositeAlphaFlagsKHR::OPAQUE)
        .present_mode(vk::PresentModeKHR::FIFO)
        .clipped(true);
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
        vk::DescriptorSetLayoutBinding::default()
            .binding(0)
            .descriptor_type(vk::DescriptorType::STORAGE_IMAGE)
            .descriptor_count(1)
            .stage_flags(vk::ShaderStageFlags::COMPUTE),
        vk::DescriptorSetLayoutBinding::default()
            .binding(1)
            .descriptor_type(vk::DescriptorType::STORAGE_BUFFER)
            .descriptor_count(1)
            .stage_flags(vk::ShaderStageFlags::COMPUTE),
    ];
    let layout_info = vk::DescriptorSetLayoutCreateInfo::default().bindings(&bindings);
    RTR_DESCRIPTOR_SET_LAYOUT = device().create_descriptor_set_layout(&layout_info, None)?;

    let pool_sizes = [
        vk::DescriptorPoolSize::default()
            .ty(vk::DescriptorType::STORAGE_IMAGE)
            .descriptor_count(RTR_MAX_SWAP_IMAGES as u32),
        vk::DescriptorPoolSize::default()
            .ty(vk::DescriptorType::STORAGE_BUFFER)
            .descriptor_count(RTR_MAX_SWAP_IMAGES as u32),
    ];
    let pool_info = vk::DescriptorPoolCreateInfo::default()
        .max_sets(RTR_MAX_SWAP_IMAGES as u32)
        .pool_sizes(&pool_sizes);
    RTR_DESCRIPTOR_POOL = device().create_descriptor_pool(&pool_info, None)?;

    let set_layouts = [RTR_DESCRIPTOR_SET_LAYOUT; RTR_MAX_SWAP_IMAGES];
    let set_info = vk::DescriptorSetAllocateInfo::default()
        .descriptor_pool(RTR_DESCRIPTOR_POOL)
        .set_layouts(&set_layouts);
    let descriptor_sets = device().allocate_descriptor_sets(&set_info)?;
    i = 0;
    while i < RTR_MAX_SWAP_IMAGES {
        RTR_DESCRIPTOR_SETS[i] = descriptor_sets[i];
        i += 1;
    }

    let pipeline_set_layouts = [RTR_DESCRIPTOR_SET_LAYOUT];
    let pipeline_layout_info =
        vk::PipelineLayoutCreateInfo::default().set_layouts(&pipeline_set_layouts);
    RTR_PIPELINE_LAYOUT = device().create_pipeline_layout(&pipeline_layout_info, None)?;

    let shader_info = vk::ShaderModuleCreateInfo::default().code(&TRACE_COMP_SPV);
    let shader_module = device().create_shader_module(&shader_info, None)?;
    let stage_info = vk::PipelineShaderStageCreateInfo::default()
        .stage(vk::ShaderStageFlags::COMPUTE)
        .module(shader_module)
        .name(c"main");
    let pipeline_info = vk::ComputePipelineCreateInfo::default()
        .stage(stage_info)
        .layout(RTR_PIPELINE_LAYOUT);
    let pipeline_infos = [pipeline_info];
    let pipeline_result =
        device().create_compute_pipelines(vk::PipelineCache::null(), &pipeline_infos, None);
    device().destroy_shader_module(shader_module, None);
    match pipeline_result {
        Ok(pipelines) => RTR_PIPELINE = pipelines[0],
        Err((_pipelines, result)) => return Err(result),
    }

    let command_pool_info = vk::CommandPoolCreateInfo::default().queue_family_index(0);
    RTR_COMMAND_POOL = device().create_command_pool(&command_pool_info, None)?;

    let command_alloc_info = vk::CommandBufferAllocateInfo::default()
        .command_pool(RTR_COMMAND_POOL)
        .level(vk::CommandBufferLevel::PRIMARY)
        .command_buffer_count(swap_image_count as u32);
    let command_buffers = device().allocate_command_buffers(&command_alloc_info)?;
    i = 0;
    while i < swap_image_count {
        RTR_COMMAND_BUFFERS[i] = command_buffers[i];
        i += 1;
    }

    let image_range = vk::ImageSubresourceRange::default()
        .aspect_mask(vk::ImageAspectFlags::COLOR)
        .base_mip_level(0)
        .level_count(1)
        .base_array_layer(0)
        .layer_count(1);

    i = 0;
    while i < swap_image_count {
        let image_view_info = vk::ImageViewCreateInfo::default()
            .image(RTR_SWAP_IMAGES[i])
            .view_type(vk::ImageViewType::TYPE_2D)
            .format(vk::Format::B8G8R8A8_UNORM)
            .components(vk::ComponentMapping::default())
            .subresource_range(image_range);
        RTR_SWAP_IMAGE_VIEWS[i] = device().create_image_view(&image_view_info, None)?;

        let image_infos = [vk::DescriptorImageInfo::default()
            .image_view(RTR_SWAP_IMAGE_VIEWS[i])
            .image_layout(vk::ImageLayout::GENERAL)];
        let buffer_infos = [vk::DescriptorBufferInfo::default()
            .buffer(RTR_MEMORY_BUFFER)
            .offset(0)
            .range(vk::WHOLE_SIZE)];
        let writes = [
            vk::WriteDescriptorSet::default()
                .dst_set(RTR_DESCRIPTOR_SETS[i])
                .dst_binding(0)
                .descriptor_type(vk::DescriptorType::STORAGE_IMAGE)
                .image_info(&image_infos),
            vk::WriteDescriptorSet::default()
                .dst_set(RTR_DESCRIPTOR_SETS[i])
                .dst_binding(1)
                .descriptor_type(vk::DescriptorType::STORAGE_BUFFER)
                .buffer_info(&buffer_infos),
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

        let to_general = [vk::ImageMemoryBarrier::default()
            .dst_access_mask(vk::AccessFlags::SHADER_WRITE)
            .old_layout(vk::ImageLayout::UNDEFINED)
            .new_layout(vk::ImageLayout::GENERAL)
            .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
            .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
            .image(RTR_SWAP_IMAGES[i])
            .subresource_range(image_range)];
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

        let memory_barrier = [vk::BufferMemoryBarrier::default()
            .src_access_mask(vk::AccessFlags::HOST_WRITE | vk::AccessFlags::SHADER_WRITE)
            .dst_access_mask(vk::AccessFlags::SHADER_READ | vk::AccessFlags::SHADER_WRITE)
            .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
            .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
            .buffer(RTR_MEMORY_BUFFER)
            .offset(0)
            .size(vk::WHOLE_SIZE)];
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

        let to_present = [vk::ImageMemoryBarrier::default()
            .src_access_mask(vk::AccessFlags::SHADER_WRITE)
            .old_layout(vk::ImageLayout::GENERAL)
            .new_layout(vk::ImageLayout::PRESENT_SRC_KHR)
            .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
            .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
            .image(RTR_SWAP_IMAGES[i])
            .subresource_range(image_range)];
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

    let fence_info = vk::FenceCreateInfo::default().flags(vk::FenceCreateFlags::SIGNALED);
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
    let submit = vk::SubmitInfo::default()
        .wait_semaphores(&wait_semaphores)
        .wait_dst_stage_mask(&wait_stages)
        .command_buffers(&command_buffers)
        .signal_semaphores(&signal_semaphores);
    let submits = [submit];
    let _ = device().queue_submit(RTR_QUEUE, &submits, RTR_IN_FLIGHT_FENCE);

    let swapchains = [RTR_SWAPCHAIN];
    let image_indices = [image_index];
    let present = vk::PresentInfoKHR::default()
        .wait_semaphores(&signal_semaphores)
        .swapchains(&swapchains)
        .image_indices(&image_indices);
    let _ = swapchain_loader().queue_present(RTR_QUEUE, &present);

    if RTR_TIMING_SUPPORTED != 0 {
        RTR_TIMING_PENDING = 1;
        RTR_TIMING_IMAGE_INDEX = image_index;
        RTR_TIMING_FRAME_INDEX = RTR_FRAME_INDEX;
    }

    RTR_FRAME_INDEX += 1;
}
