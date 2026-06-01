#![allow(static_mut_refs)]

use crate::macos;
use crate::vk::*;
use std::ffi::{c_char, c_int, c_void};
use std::mem::{size_of, zeroed};
use std::ptr::{null, null_mut};

include!(concat!(env!("OUT_DIR"), "/trace_comp_spv.rs"));

pub type RtrResult<T = ()> = Result<T, i32>;

const RTR_ERROR: i32 = -1;
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

static mut RTR_INSTANCE: VkInstance = null_mut();
static mut RTR_SURFACE: VkSurfaceKHR = null_mut();
static mut RTR_PHYSICAL_DEVICE: VkPhysicalDevice = null_mut();
static mut RTR_DEVICE: VkDevice = null_mut();
static mut RTR_QUEUE: VkQueue = null_mut();
static mut RTR_SWAPCHAIN: VkSwapchainKHR = null_mut();
static mut RTR_SWAP_EXTENT: VkExtent2D = VkExtent2D {
    width: 0,
    height: 0,
};
static mut RTR_SWAP_IMAGES: [VkImage; RTR_MAX_SWAP_IMAGES] = [null_mut(); RTR_MAX_SWAP_IMAGES];
static mut RTR_SWAP_IMAGE_VIEWS: [VkImageView; RTR_MAX_SWAP_IMAGES] =
    [null_mut(); RTR_MAX_SWAP_IMAGES];
static mut RTR_DESCRIPTOR_SET_LAYOUT: VkDescriptorSetLayout = null_mut();
static mut RTR_DESCRIPTOR_POOL: VkDescriptorPool = null_mut();
static mut RTR_DESCRIPTOR_SETS: [VkDescriptorSet; RTR_MAX_SWAP_IMAGES] =
    [null_mut(); RTR_MAX_SWAP_IMAGES];
static mut RTR_PIPELINE_LAYOUT: VkPipelineLayout = null_mut();
static mut RTR_PIPELINE: VkPipeline = null_mut();
static mut RTR_COMMAND_POOL: VkCommandPool = null_mut();
static mut RTR_COMMAND_BUFFERS: [VkCommandBuffer; RTR_MAX_SWAP_IMAGES] =
    [null_mut(); RTR_MAX_SWAP_IMAGES];
static mut RTR_IMAGE_AVAILABLE_SEMAPHORE: VkSemaphore = null_mut();
static mut RTR_RENDER_FINISHED_SEMAPHORE: VkSemaphore = null_mut();
static mut RTR_IN_FLIGHT_FENCE: VkFence = null_mut();
static mut RTR_TIMING_QUERY_POOL: VkQueryPool = null_mut();
static mut RTR_MEMORY_BUFFER: VkBuffer = null_mut();
static mut RTR_MEMORY_BUFFER_MEMORY: VkDeviceMemory = null_mut();
static mut RTR_MEMORY_WORDS: *mut u32 = null_mut();
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

fn vk_check(result: VkResult) -> RtrResult {
    if result == VK_SUCCESS {
        Ok(())
    } else {
        Err(result)
    }
}

fn vk_check_incomplete(result: VkResult) -> RtrResult {
    if result == VK_SUCCESS || result == VK_INCOMPLETE {
        Ok(())
    } else {
        Err(result)
    }
}

unsafe fn create_instance() -> RtrResult {
    let app = b"realtimerays\0";
    let instance_exts = [
        b"VK_KHR_surface\0".as_ptr().cast::<c_char>(),
        b"VK_EXT_metal_surface\0".as_ptr().cast::<c_char>(),
        b"VK_KHR_portability_enumeration\0"
            .as_ptr()
            .cast::<c_char>(),
    ];
    let app_info = VkApplicationInfo {
        sType: VK_STRUCTURE_TYPE_APPLICATION_INFO,
        pNext: null(),
        pApplicationName: app.as_ptr().cast(),
        applicationVersion: vk_make_api_version(0, 0, 1, 0),
        pEngineName: app.as_ptr().cast(),
        engineVersion: vk_make_api_version(0, 0, 1, 0),
        apiVersion: VK_API_VERSION_1_3,
    };
    let create_info = VkInstanceCreateInfo {
        sType: VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        pNext: null(),
        flags: VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
        pApplicationInfo: &app_info,
        enabledLayerCount: 0,
        ppEnabledLayerNames: null(),
        enabledExtensionCount: instance_exts.len() as u32,
        ppEnabledExtensionNames: instance_exts.as_ptr(),
    };
    vk_check(vkCreateInstance(&create_info, null(), &mut RTR_INSTANCE))
}

unsafe fn create_device() -> RtrResult {
    let priority = 1.0f32;
    let device_exts = [
        b"VK_KHR_swapchain\0".as_ptr().cast::<c_char>(),
        b"VK_KHR_portability_subset\0".as_ptr().cast::<c_char>(),
    ];
    let queue_info = VkDeviceQueueCreateInfo {
        sType: VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        pNext: null(),
        flags: 0,
        queueFamilyIndex: 0,
        queueCount: 1,
        pQueuePriorities: &priority,
    };
    let create_info = VkDeviceCreateInfo {
        sType: VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        pNext: null(),
        flags: 0,
        queueCreateInfoCount: 1,
        pQueueCreateInfos: &queue_info,
        enabledLayerCount: 0,
        ppEnabledLayerNames: null(),
        enabledExtensionCount: device_exts.len() as u32,
        ppEnabledExtensionNames: device_exts.as_ptr(),
        pEnabledFeatures: null(),
    };
    vk_check(vkCreateDevice(
        RTR_PHYSICAL_DEVICE,
        &create_info,
        null(),
        &mut RTR_DEVICE,
    ))?;
    vkGetDeviceQueue(RTR_DEVICE, 0, 0, &mut RTR_QUEUE);
    Ok(())
}

unsafe fn find_memory_type(type_bits: u32, flags: VkMemoryPropertyFlags) -> u32 {
    let mut props: VkPhysicalDeviceMemoryProperties = zeroed();
    vkGetPhysicalDeviceMemoryProperties(RTR_PHYSICAL_DEVICE, &mut props);

    let mut i = 0;
    while i < props.memoryTypeCount {
        let memory_type = props.memoryTypes[i as usize];
        if (type_bits & (1u32 << i)) != 0 && (memory_type.propertyFlags & flags) == flags {
            return i;
        }
        i += 1;
    }

    u32::MAX
}

unsafe fn create_memory_buffer() -> RtrResult {
    let history_words = u64::from(RTR_SWAP_EXTENT.width)
        * u64::from(RTR_SWAP_EXTENT.height)
        * RTR_HISTORY_WORDS_PER_PIXEL
        * RTR_HISTORY_PAGE_COUNT;
    let memory_words = RTR_MEMORY_HEADER_WORDS + history_words;
    let memory_size = memory_words * size_of::<u32>() as u64;

    let buffer_info = VkBufferCreateInfo {
        sType: VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        pNext: null(),
        flags: 0,
        size: memory_size,
        usage: VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        sharingMode: VK_SHARING_MODE_EXCLUSIVE,
        queueFamilyIndexCount: 0,
        pQueueFamilyIndices: null(),
    };
    vk_check(vkCreateBuffer(
        RTR_DEVICE,
        &buffer_info,
        null(),
        &mut RTR_MEMORY_BUFFER,
    ))?;

    let mut requirements: VkMemoryRequirements = zeroed();
    vkGetBufferMemoryRequirements(RTR_DEVICE, RTR_MEMORY_BUFFER, &mut requirements);
    let memory_type = find_memory_type(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    );
    if memory_type == u32::MAX {
        return Err(RTR_ERROR);
    }

    let alloc_info = VkMemoryAllocateInfo {
        sType: VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        pNext: null(),
        allocationSize: requirements.size,
        memoryTypeIndex: memory_type,
    };
    vk_check(vkAllocateMemory(
        RTR_DEVICE,
        &alloc_info,
        null(),
        &mut RTR_MEMORY_BUFFER_MEMORY,
    ))?;
    vk_check(vkBindBufferMemory(
        RTR_DEVICE,
        RTR_MEMORY_BUFFER,
        RTR_MEMORY_BUFFER_MEMORY,
        0,
    ))?;

    let mut mapped: *mut c_void = null_mut();
    vk_check(vkMapMemory(
        RTR_DEVICE,
        RTR_MEMORY_BUFFER_MEMORY,
        0,
        memory_size,
        0,
        &mut mapped,
    ))?;
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

unsafe fn create_timing_query_pool() -> RtrResult {
    if RTR_TIMING_SUPPORTED == 0 {
        return Ok(());
    }

    let info = VkQueryPoolCreateInfo {
        sType: VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        pNext: null(),
        flags: 0,
        queryType: VK_QUERY_TYPE_TIMESTAMP,
        queryCount: (RTR_MAX_SWAP_IMAGES * 2) as u32,
        pipelineStatistics: 0,
    };
    if vkCreateQueryPool(RTR_DEVICE, &info, null(), &mut RTR_TIMING_QUERY_POOL) != VK_SUCCESS {
        RTR_TIMING_SUPPORTED = 0;
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
    let result = vkGetQueryPoolResults(
        RTR_DEVICE,
        RTR_TIMING_QUERY_POOL,
        query,
        2,
        size_of::<[u64; 2]>(),
        timestamp.as_mut_ptr().cast(),
        size_of::<u64>() as u64,
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT,
    );
    if result != VK_SUCCESS {
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

pub unsafe fn init(window_surface: *mut c_void) -> RtrResult {
    clock_gettime(CLOCK_MONOTONIC, &mut RTR_START_TIME);
    RTR_FRAME_INDEX = 0;
    RTR_TIMING_PENDING = 0;

    create_instance()?;

    let metal_info = VkMetalSurfaceCreateInfoEXT {
        sType: VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
        pNext: null(),
        flags: 0,
        pLayer: window_surface.cast_const(),
    };
    vk_check(vkCreateMetalSurfaceEXT(
        RTR_INSTANCE,
        &metal_info,
        null(),
        &mut RTR_SURFACE,
    ))?;

    let mut device_count = 1u32;
    vk_check_incomplete(vkEnumeratePhysicalDevices(
        RTR_INSTANCE,
        &mut device_count,
        &mut RTR_PHYSICAL_DEVICE,
    ))?;
    if device_count == 0 || RTR_PHYSICAL_DEVICE.is_null() {
        return Err(RTR_ERROR);
    }

    let mut device_props: VkPhysicalDeviceProperties = zeroed();
    vkGetPhysicalDeviceProperties(RTR_PHYSICAL_DEVICE, &mut device_props);
    let mut queue_family_count = 1u32;
    let mut queue_family_props: VkQueueFamilyProperties = zeroed();
    vkGetPhysicalDeviceQueueFamilyProperties(
        RTR_PHYSICAL_DEVICE,
        &mut queue_family_count,
        &mut queue_family_props,
    );
    RTR_TIMESTAMP_PERIOD = device_props.limits.timestampPeriod;
    RTR_TIMESTAMP_VALID_BITS = queue_family_props.timestampValidBits;
    RTR_TIMING_SUPPORTED = u32::from(
        device_props.limits.timestampComputeAndGraphics != 0
            && RTR_TIMESTAMP_VALID_BITS > 0
            && RTR_TIMESTAMP_PERIOD > 0.0,
    );

    create_device()?;

    let mut caps: VkSurfaceCapabilitiesKHR = zeroed();
    vk_check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        RTR_PHYSICAL_DEVICE,
        RTR_SURFACE,
        &mut caps,
    ))?;
    RTR_SWAP_EXTENT = caps.currentExtent;
    let mut min_image_count = RTR_MAX_SWAP_IMAGES as u32;
    if min_image_count < caps.minImageCount {
        min_image_count = caps.minImageCount;
    }
    if caps.maxImageCount != 0 && min_image_count > caps.maxImageCount {
        min_image_count = caps.maxImageCount;
    }

    let swapchain_info = VkSwapchainCreateInfoKHR {
        sType: VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        pNext: null(),
        flags: 0,
        surface: RTR_SURFACE,
        minImageCount: min_image_count,
        imageFormat: VK_FORMAT_B8G8R8A8_UNORM,
        imageColorSpace: VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        imageExtent: RTR_SWAP_EXTENT,
        imageArrayLayers: 1,
        imageUsage: VK_IMAGE_USAGE_STORAGE_BIT,
        imageSharingMode: VK_SHARING_MODE_EXCLUSIVE,
        queueFamilyIndexCount: 0,
        pQueueFamilyIndices: null(),
        preTransform: caps.currentTransform,
        compositeAlpha: VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        presentMode: VK_PRESENT_MODE_FIFO_KHR,
        clipped: VK_TRUE,
        oldSwapchain: null_mut(),
    };
    vk_check(vkCreateSwapchainKHR(
        RTR_DEVICE,
        &swapchain_info,
        null(),
        &mut RTR_SWAPCHAIN,
    ))?;

    let mut swap_image_count = 0u32;
    vk_check(vkGetSwapchainImagesKHR(
        RTR_DEVICE,
        RTR_SWAPCHAIN,
        &mut swap_image_count,
        null_mut(),
    ))?;
    if swap_image_count < 2 || swap_image_count > RTR_MAX_SWAP_IMAGES as u32 {
        return Err(RTR_ERROR);
    }
    vk_check(vkGetSwapchainImagesKHR(
        RTR_DEVICE,
        RTR_SWAPCHAIN,
        &mut swap_image_count,
        RTR_SWAP_IMAGES.as_mut_ptr(),
    ))?;

    create_memory_buffer()?;
    create_timing_query_pool()?;

    let bindings = [
        VkDescriptorSetLayoutBinding {
            binding: 0,
            descriptorType: VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            descriptorCount: 1,
            stageFlags: VK_SHADER_STAGE_COMPUTE_BIT,
            pImmutableSamplers: null(),
        },
        VkDescriptorSetLayoutBinding {
            binding: 1,
            descriptorType: VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            descriptorCount: 1,
            stageFlags: VK_SHADER_STAGE_COMPUTE_BIT,
            pImmutableSamplers: null(),
        },
    ];
    let layout_info = VkDescriptorSetLayoutCreateInfo {
        sType: VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        pNext: null(),
        flags: 0,
        bindingCount: bindings.len() as u32,
        pBindings: bindings.as_ptr(),
    };
    vk_check(vkCreateDescriptorSetLayout(
        RTR_DEVICE,
        &layout_info,
        null(),
        &mut RTR_DESCRIPTOR_SET_LAYOUT,
    ))?;

    let pool_sizes = [
        VkDescriptorPoolSize {
            type_: VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            descriptorCount: RTR_MAX_SWAP_IMAGES as u32,
        },
        VkDescriptorPoolSize {
            type_: VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            descriptorCount: RTR_MAX_SWAP_IMAGES as u32,
        },
    ];
    let pool_info = VkDescriptorPoolCreateInfo {
        sType: VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        pNext: null(),
        flags: 0,
        maxSets: RTR_MAX_SWAP_IMAGES as u32,
        poolSizeCount: pool_sizes.len() as u32,
        pPoolSizes: pool_sizes.as_ptr(),
    };
    vk_check(vkCreateDescriptorPool(
        RTR_DEVICE,
        &pool_info,
        null(),
        &mut RTR_DESCRIPTOR_POOL,
    ))?;

    let set_layouts = [RTR_DESCRIPTOR_SET_LAYOUT; RTR_MAX_SWAP_IMAGES];
    let set_info = VkDescriptorSetAllocateInfo {
        sType: VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        pNext: null(),
        descriptorPool: RTR_DESCRIPTOR_POOL,
        descriptorSetCount: RTR_MAX_SWAP_IMAGES as u32,
        pSetLayouts: set_layouts.as_ptr(),
    };
    vk_check(vkAllocateDescriptorSets(
        RTR_DEVICE,
        &set_info,
        RTR_DESCRIPTOR_SETS.as_mut_ptr(),
    ))?;

    let pipeline_layout_info = VkPipelineLayoutCreateInfo {
        sType: VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        pNext: null(),
        flags: 0,
        setLayoutCount: 1,
        pSetLayouts: &RTR_DESCRIPTOR_SET_LAYOUT,
        pushConstantRangeCount: 0,
        pPushConstantRanges: null(),
    };
    vk_check(vkCreatePipelineLayout(
        RTR_DEVICE,
        &pipeline_layout_info,
        null(),
        &mut RTR_PIPELINE_LAYOUT,
    ))?;

    let mut shader_module: VkShaderModule = null_mut();
    let shader_info = VkShaderModuleCreateInfo {
        sType: VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        pNext: null(),
        flags: 0,
        codeSize: TRACE_COMP_SPV.len() * size_of::<u32>(),
        pCode: TRACE_COMP_SPV.as_ptr(),
    };
    vk_check(vkCreateShaderModule(
        RTR_DEVICE,
        &shader_info,
        null(),
        &mut shader_module,
    ))?;
    let stage_info = VkPipelineShaderStageCreateInfo {
        sType: VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        pNext: null(),
        flags: 0,
        stage: VK_SHADER_STAGE_COMPUTE_BIT,
        module: shader_module,
        pName: b"main\0".as_ptr().cast(),
        pSpecializationInfo: null(),
    };
    let pipeline_info = VkComputePipelineCreateInfo {
        sType: VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        pNext: null(),
        flags: 0,
        stage: stage_info,
        layout: RTR_PIPELINE_LAYOUT,
        basePipelineHandle: null_mut(),
        basePipelineIndex: -1,
    };
    let pipeline_result = vkCreateComputePipelines(
        RTR_DEVICE,
        null_mut(),
        1,
        &pipeline_info,
        null(),
        &mut RTR_PIPELINE,
    );
    vkDestroyShaderModule(RTR_DEVICE, shader_module, null());
    vk_check(pipeline_result)?;

    let command_pool_info = VkCommandPoolCreateInfo {
        sType: VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        pNext: null(),
        flags: 0,
        queueFamilyIndex: 0,
    };
    vk_check(vkCreateCommandPool(
        RTR_DEVICE,
        &command_pool_info,
        null(),
        &mut RTR_COMMAND_POOL,
    ))?;

    let command_alloc_info = VkCommandBufferAllocateInfo {
        sType: VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        pNext: null(),
        commandPool: RTR_COMMAND_POOL,
        level: VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        commandBufferCount: swap_image_count,
    };
    vk_check(vkAllocateCommandBuffers(
        RTR_DEVICE,
        &command_alloc_info,
        RTR_COMMAND_BUFFERS.as_mut_ptr(),
    ))?;

    let image_range = VkImageSubresourceRange {
        aspectMask: VK_IMAGE_ASPECT_COLOR_BIT,
        baseMipLevel: 0,
        levelCount: 1,
        baseArrayLayer: 0,
        layerCount: 1,
    };

    let mut i = 0usize;
    while i < swap_image_count as usize {
        let image_view_info = VkImageViewCreateInfo {
            sType: VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            pNext: null(),
            flags: 0,
            image: RTR_SWAP_IMAGES[i],
            viewType: VK_IMAGE_VIEW_TYPE_2D,
            format: VK_FORMAT_B8G8R8A8_UNORM,
            components: zeroed(),
            subresourceRange: image_range,
        };
        vk_check(vkCreateImageView(
            RTR_DEVICE,
            &image_view_info,
            null(),
            &mut RTR_SWAP_IMAGE_VIEWS[i],
        ))?;

        let image_info = VkDescriptorImageInfo {
            sampler: null_mut(),
            imageView: RTR_SWAP_IMAGE_VIEWS[i],
            imageLayout: VK_IMAGE_LAYOUT_GENERAL,
        };
        let buffer_info = VkDescriptorBufferInfo {
            buffer: RTR_MEMORY_BUFFER,
            offset: 0,
            range: VK_WHOLE_SIZE,
        };
        let writes = [
            VkWriteDescriptorSet {
                sType: VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                pNext: null(),
                dstSet: RTR_DESCRIPTOR_SETS[i],
                dstBinding: 0,
                dstArrayElement: 0,
                descriptorCount: 1,
                descriptorType: VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                pImageInfo: &image_info,
                pBufferInfo: null(),
                pTexelBufferView: null(),
            },
            VkWriteDescriptorSet {
                sType: VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                pNext: null(),
                dstSet: RTR_DESCRIPTOR_SETS[i],
                dstBinding: 1,
                dstArrayElement: 0,
                descriptorCount: 1,
                descriptorType: VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                pImageInfo: null(),
                pBufferInfo: &buffer_info,
                pTexelBufferView: null(),
            },
        ];
        vkUpdateDescriptorSets(RTR_DEVICE, writes.len() as u32, writes.as_ptr(), 0, null());

        let begin_info = VkCommandBufferBeginInfo {
            sType: VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            pNext: null(),
            flags: 0,
            pInheritanceInfo: null(),
        };
        vk_check(vkBeginCommandBuffer(RTR_COMMAND_BUFFERS[i], &begin_info))?;

        if RTR_TIMING_SUPPORTED != 0 {
            vkCmdResetQueryPool(
                RTR_COMMAND_BUFFERS[i],
                RTR_TIMING_QUERY_POOL,
                (i as u32) * 2,
                2,
            );
        }

        let to_general = VkImageMemoryBarrier {
            sType: VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            pNext: null(),
            srcAccessMask: 0,
            dstAccessMask: VK_ACCESS_SHADER_WRITE_BIT,
            oldLayout: VK_IMAGE_LAYOUT_UNDEFINED,
            newLayout: VK_IMAGE_LAYOUT_GENERAL,
            srcQueueFamilyIndex: VK_QUEUE_FAMILY_IGNORED,
            dstQueueFamilyIndex: VK_QUEUE_FAMILY_IGNORED,
            image: RTR_SWAP_IMAGES[i],
            subresourceRange: image_range,
        };
        vkCmdPipelineBarrier(
            RTR_COMMAND_BUFFERS[i],
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0,
            null(),
            0,
            null(),
            1,
            &to_general,
        );

        if RTR_TIMING_SUPPORTED != 0 {
            vkCmdWriteTimestamp(
                RTR_COMMAND_BUFFERS[i],
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                RTR_TIMING_QUERY_POOL,
                (i as u32) * 2,
            );
        }

        let memory_barrier = VkBufferMemoryBarrier {
            sType: VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            pNext: null(),
            srcAccessMask: VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            dstAccessMask: VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            srcQueueFamilyIndex: VK_QUEUE_FAMILY_IGNORED,
            dstQueueFamilyIndex: VK_QUEUE_FAMILY_IGNORED,
            buffer: RTR_MEMORY_BUFFER,
            offset: 0,
            size: VK_WHOLE_SIZE,
        };
        vkCmdPipelineBarrier(
            RTR_COMMAND_BUFFERS[i],
            VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0,
            null(),
            1,
            &memory_barrier,
            0,
            null(),
        );

        vkCmdBindPipeline(
            RTR_COMMAND_BUFFERS[i],
            VK_PIPELINE_BIND_POINT_COMPUTE,
            RTR_PIPELINE,
        );
        vkCmdBindDescriptorSets(
            RTR_COMMAND_BUFFERS[i],
            VK_PIPELINE_BIND_POINT_COMPUTE,
            RTR_PIPELINE_LAYOUT,
            0,
            1,
            &RTR_DESCRIPTOR_SETS[i],
            0,
            null(),
        );
        vkCmdDispatch(
            RTR_COMMAND_BUFFERS[i],
            (RTR_SWAP_EXTENT.width + RTR_TILE_SIZE - 1) / RTR_TILE_SIZE,
            (RTR_SWAP_EXTENT.height + RTR_TILE_SIZE - 1) / RTR_TILE_SIZE,
            1,
        );

        if RTR_TIMING_SUPPORTED != 0 {
            vkCmdWriteTimestamp(
                RTR_COMMAND_BUFFERS[i],
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                RTR_TIMING_QUERY_POOL,
                (i as u32) * 2 + 1,
            );
        }

        let to_present = VkImageMemoryBarrier {
            sType: VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            pNext: null(),
            srcAccessMask: VK_ACCESS_SHADER_WRITE_BIT,
            dstAccessMask: 0,
            oldLayout: VK_IMAGE_LAYOUT_GENERAL,
            newLayout: VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            srcQueueFamilyIndex: VK_QUEUE_FAMILY_IGNORED,
            dstQueueFamilyIndex: VK_QUEUE_FAMILY_IGNORED,
            image: RTR_SWAP_IMAGES[i],
            subresourceRange: image_range,
        };
        vkCmdPipelineBarrier(
            RTR_COMMAND_BUFFERS[i],
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0,
            null(),
            0,
            null(),
            1,
            &to_present,
        );

        vk_check(vkEndCommandBuffer(RTR_COMMAND_BUFFERS[i]))?;
        i += 1;
    }

    let semaphore_info = VkSemaphoreCreateInfo {
        sType: VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        pNext: null(),
        flags: 0,
    };
    vk_check(vkCreateSemaphore(
        RTR_DEVICE,
        &semaphore_info,
        null(),
        &mut RTR_IMAGE_AVAILABLE_SEMAPHORE,
    ))?;
    vk_check(vkCreateSemaphore(
        RTR_DEVICE,
        &semaphore_info,
        null(),
        &mut RTR_RENDER_FINISHED_SEMAPHORE,
    ))?;

    let fence_info = VkFenceCreateInfo {
        sType: VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        pNext: null(),
        flags: VK_FENCE_CREATE_SIGNALED_BIT,
    };
    vk_check(vkCreateFence(
        RTR_DEVICE,
        &fence_info,
        null(),
        &mut RTR_IN_FLIGHT_FENCE,
    ))?;

    Ok(())
}

pub unsafe fn frame() {
    let wait_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    vkWaitForFences(RTR_DEVICE, 1, &RTR_IN_FLIGHT_FENCE, VK_TRUE, u64::MAX);
    report_gpu_timing();
    update_memory();
    vkResetFences(RTR_DEVICE, 1, &RTR_IN_FLIGHT_FENCE);

    let mut image_index = 0u32;
    vkAcquireNextImageKHR(
        RTR_DEVICE,
        RTR_SWAPCHAIN,
        u64::MAX,
        RTR_IMAGE_AVAILABLE_SEMAPHORE,
        null_mut(),
        &mut image_index,
    );

    let submit = VkSubmitInfo {
        sType: VK_STRUCTURE_TYPE_SUBMIT_INFO,
        pNext: null(),
        waitSemaphoreCount: 1,
        pWaitSemaphores: &RTR_IMAGE_AVAILABLE_SEMAPHORE,
        pWaitDstStageMask: &wait_stage,
        commandBufferCount: 1,
        pCommandBuffers: &RTR_COMMAND_BUFFERS[image_index as usize],
        signalSemaphoreCount: 1,
        pSignalSemaphores: &RTR_RENDER_FINISHED_SEMAPHORE,
    };
    vkQueueSubmit(RTR_QUEUE, 1, &submit, RTR_IN_FLIGHT_FENCE);

    let present = VkPresentInfoKHR {
        sType: VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        pNext: null(),
        waitSemaphoreCount: 1,
        pWaitSemaphores: &RTR_RENDER_FINISHED_SEMAPHORE,
        swapchainCount: 1,
        pSwapchains: &RTR_SWAPCHAIN,
        pImageIndices: &image_index,
        pResults: null_mut(),
    };
    vkQueuePresentKHR(RTR_QUEUE, &present);

    if RTR_TIMING_SUPPORTED != 0 {
        RTR_TIMING_PENDING = 1;
        RTR_TIMING_IMAGE_INDEX = image_index;
        RTR_TIMING_FRAME_INDEX = RTR_FRAME_INDEX;
    }

    RTR_FRAME_INDEX += 1;
}
