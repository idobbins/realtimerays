#![allow(non_camel_case_types)]
#![allow(dead_code)]
#![allow(unused_unsafe)]

use std::ffi::{c_char, c_void, CStr};
use std::marker::PhantomData;
use std::mem::{size_of, size_of_val, MaybeUninit};
use std::ptr;

pub type Bool32 = u32;
pub type DeviceSize = u64;
pub type VkResult<T> = core::result::Result<T, Result>;

pub const FALSE: Bool32 = 0;
pub const TRUE: Bool32 = 1;
pub const QUEUE_FAMILY_IGNORED: u32 = u32::MAX;
pub const WHOLE_SIZE: DeviceSize = u64::MAX;
pub const API_VERSION_1_3: u32 = make_api_version(0, 1, 3, 0);

pub const fn make_api_version(variant: u32, major: u32, minor: u32, patch: u32) -> u32 {
    (variant << 29) | (major << 22) | (minor << 12) | patch
}

macro_rules! dispatch_handle {
    ($name:ident) => {
        #[repr(transparent)]
        #[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
        pub struct $name(pub *mut c_void);

        impl $name {
            pub const fn null() -> Self {
                Self(ptr::null_mut())
            }
        }

        unsafe impl Send for $name {}
        unsafe impl Sync for $name {}
    };
}

macro_rules! nondispatch_handle {
    ($name:ident) => {
        #[repr(transparent)]
        #[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
        pub struct $name(pub u64);

        impl $name {
            pub const fn null() -> Self {
                Self(0)
            }
        }
    };
}

macro_rules! vk_enum {
    ($name:ident) => {
        #[repr(transparent)]
        #[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
        pub struct $name(pub i32);

        impl $name {
            pub const fn from_raw(raw: i32) -> Self {
                Self(raw)
            }

            pub const fn as_raw(self) -> i32 {
                self.0
            }
        }
    };
}

macro_rules! vk_flags {
    ($name:ident) => {
        #[repr(transparent)]
        #[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
        pub struct $name(pub u32);

        impl $name {
            pub const fn empty() -> Self {
                Self(0)
            }

            pub const fn from_raw(raw: u32) -> Self {
                Self(raw)
            }

            pub const fn as_raw(self) -> u32 {
                self.0
            }

            pub const fn contains(self, other: Self) -> bool {
                self.0 & other.0 == other.0
            }
        }

        impl core::ops::BitOr for $name {
            type Output = Self;

            fn bitor(self, rhs: Self) -> Self {
                Self(self.0 | rhs.0)
            }
        }

        impl core::ops::BitOrAssign for $name {
            fn bitor_assign(&mut self, rhs: Self) {
                self.0 |= rhs.0;
            }
        }

        impl core::ops::BitAnd for $name {
            type Output = Self;

            fn bitand(self, rhs: Self) -> Self {
                Self(self.0 & rhs.0)
            }
        }
    };
}

dispatch_handle!(Instance);
dispatch_handle!(PhysicalDevice);
dispatch_handle!(Device);
dispatch_handle!(Queue);
dispatch_handle!(CommandBuffer);

nondispatch_handle!(Semaphore);
nondispatch_handle!(Fence);
nondispatch_handle!(DeviceMemory);
nondispatch_handle!(Buffer);
nondispatch_handle!(Image);
nondispatch_handle!(QueryPool);
nondispatch_handle!(ImageView);
nondispatch_handle!(CommandPool);
nondispatch_handle!(RenderPass);
nondispatch_handle!(Framebuffer);
nondispatch_handle!(Event);
nondispatch_handle!(BufferView);
nondispatch_handle!(ShaderModule);
nondispatch_handle!(PipelineCache);
nondispatch_handle!(Pipeline);
nondispatch_handle!(PipelineLayout);
nondispatch_handle!(DescriptorSetLayout);
nondispatch_handle!(Sampler);
nondispatch_handle!(DescriptorSet);
nondispatch_handle!(DescriptorPool);
nondispatch_handle!(SurfaceKHR);
nondispatch_handle!(SwapchainKHR);

#[repr(transparent)]
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Result(pub i32);

impl Result {
    pub const SUCCESS: Self = Self(0);
    pub const NOT_READY: Self = Self(1);
    pub const TIMEOUT: Self = Self(2);
    pub const INCOMPLETE: Self = Self(5);
    pub const ERROR_INITIALIZATION_FAILED: Self = Self(-3);
    pub const SUBOPTIMAL_KHR: Self = Self(1_000_001_003);
    pub const ERROR_OUT_OF_DATE_KHR: Self = Self(-1_000_001_004);

    pub fn result(self) -> VkResult<()> {
        if self == Self::SUCCESS {
            Ok(())
        } else {
            Err(self)
        }
    }

    unsafe fn assume_init_on_success<T>(self, value: MaybeUninit<T>) -> VkResult<T> {
        if self == Self::SUCCESS {
            Ok(value.assume_init())
        } else {
            Err(self)
        }
    }
}

vk_enum!(StructureType);
impl StructureType {
    pub const APPLICATION_INFO: Self = Self(0);
    pub const INSTANCE_CREATE_INFO: Self = Self(1);
    pub const DEVICE_QUEUE_CREATE_INFO: Self = Self(2);
    pub const DEVICE_CREATE_INFO: Self = Self(3);
    pub const SUBMIT_INFO: Self = Self(4);
    pub const MEMORY_ALLOCATE_INFO: Self = Self(5);
    pub const FENCE_CREATE_INFO: Self = Self(8);
    pub const SEMAPHORE_CREATE_INFO: Self = Self(9);
    pub const QUERY_POOL_CREATE_INFO: Self = Self(11);
    pub const BUFFER_CREATE_INFO: Self = Self(12);
    pub const IMAGE_CREATE_INFO: Self = Self(14);
    pub const IMAGE_VIEW_CREATE_INFO: Self = Self(15);
    pub const SHADER_MODULE_CREATE_INFO: Self = Self(16);
    pub const PIPELINE_SHADER_STAGE_CREATE_INFO: Self = Self(18);
    pub const COMPUTE_PIPELINE_CREATE_INFO: Self = Self(29);
    pub const PIPELINE_LAYOUT_CREATE_INFO: Self = Self(30);
    pub const DESCRIPTOR_SET_LAYOUT_CREATE_INFO: Self = Self(32);
    pub const DESCRIPTOR_POOL_CREATE_INFO: Self = Self(33);
    pub const DESCRIPTOR_SET_ALLOCATE_INFO: Self = Self(34);
    pub const WRITE_DESCRIPTOR_SET: Self = Self(35);
    pub const COMMAND_POOL_CREATE_INFO: Self = Self(39);
    pub const COMMAND_BUFFER_ALLOCATE_INFO: Self = Self(40);
    pub const COMMAND_BUFFER_BEGIN_INFO: Self = Self(42);
    pub const BUFFER_MEMORY_BARRIER: Self = Self(44);
    pub const IMAGE_MEMORY_BARRIER: Self = Self(45);
    pub const SWAPCHAIN_CREATE_INFO_KHR: Self = Self(1_000_001_000);
    pub const PRESENT_INFO_KHR: Self = Self(1_000_001_001);
    pub const METAL_SURFACE_CREATE_INFO_EXT: Self = Self(1_000_217_000);
}

vk_enum!(Format);
impl Format {
    pub const R8G8B8A8_UNORM: Self = Self(37);
    pub const B8G8R8A8_UNORM: Self = Self(44);
}

vk_enum!(PhysicalDeviceType);
vk_enum!(SharingMode);
impl SharingMode {
    pub const EXCLUSIVE: Self = Self(0);
}

vk_enum!(ImageLayout);
impl ImageLayout {
    pub const UNDEFINED: Self = Self(0);
    pub const GENERAL: Self = Self(1);
    pub const TRANSFER_SRC_OPTIMAL: Self = Self(6);
    pub const PRESENT_SRC_KHR: Self = Self(1_000_001_002);
}

vk_enum!(ImageTiling);
impl ImageTiling {
    pub const OPTIMAL: Self = Self(0);
}

vk_enum!(ImageType);
impl ImageType {
    pub const TYPE_2D: Self = Self(1);
}

vk_enum!(ImageViewType);
impl ImageViewType {
    pub const TYPE_2D: Self = Self(1);
}

vk_enum!(ComponentSwizzle);
vk_enum!(CommandBufferLevel);
impl CommandBufferLevel {
    pub const PRIMARY: Self = Self(0);
}

vk_enum!(DescriptorType);
impl DescriptorType {
    pub const STORAGE_IMAGE: Self = Self(3);
    pub const STORAGE_BUFFER: Self = Self(7);
}

vk_enum!(PipelineBindPoint);
impl PipelineBindPoint {
    pub const COMPUTE: Self = Self(1);
}

vk_enum!(QueryType);
impl QueryType {
    pub const TIMESTAMP: Self = Self(2);
}

vk_enum!(PresentModeKHR);
impl PresentModeKHR {
    pub const FIFO: Self = Self(2);
}

vk_enum!(ColorSpaceKHR);
impl ColorSpaceKHR {
    pub const SRGB_NONLINEAR: Self = Self(0);
}

vk_flags!(InstanceCreateFlags);
impl InstanceCreateFlags {
    pub const ENUMERATE_PORTABILITY_KHR: Self = Self(0x0000_0001);
}

vk_flags!(DeviceQueueCreateFlags);
vk_flags!(DeviceCreateFlags);
vk_flags!(BufferCreateFlags);
vk_flags!(BufferUsageFlags);
impl BufferUsageFlags {
    pub const TRANSFER_DST: Self = Self(0x0000_0002);
    pub const STORAGE_BUFFER: Self = Self(0x0000_0020);
}

vk_flags!(ImageCreateFlags);
vk_flags!(ImageUsageFlags);
impl ImageUsageFlags {
    pub const TRANSFER_SRC: Self = Self(0x0000_0001);
    pub const STORAGE: Self = Self(0x0000_0008);
}

vk_flags!(MemoryPropertyFlags);
impl MemoryPropertyFlags {
    pub const DEVICE_LOCAL: Self = Self(0x0000_0001);
    pub const HOST_VISIBLE: Self = Self(0x0000_0002);
    pub const HOST_COHERENT: Self = Self(0x0000_0004);
}

vk_flags!(MemoryHeapFlags);
vk_flags!(QueueFlags);
vk_flags!(ShaderStageFlags);
impl ShaderStageFlags {
    pub const COMPUTE: Self = Self(0x0000_0020);
}

vk_flags!(PipelineStageFlags);
impl PipelineStageFlags {
    pub const TOP_OF_PIPE: Self = Self(0x0000_0001);
    pub const COMPUTE_SHADER: Self = Self(0x0000_0800);
    pub const TRANSFER: Self = Self(0x0000_1000);
    pub const BOTTOM_OF_PIPE: Self = Self(0x0000_2000);
    pub const HOST: Self = Self(0x0000_4000);
}

vk_flags!(MemoryMapFlags);
vk_flags!(ImageAspectFlags);
impl ImageAspectFlags {
    pub const COLOR: Self = Self(0x0000_0001);
}

vk_flags!(FenceCreateFlags);
impl FenceCreateFlags {
    pub const SIGNALED: Self = Self(0x0000_0001);
}

vk_flags!(SemaphoreCreateFlags);
vk_flags!(QueryPoolCreateFlags);
vk_flags!(QueryPipelineStatisticFlags);
vk_flags!(QueryResultFlags);
impl QueryResultFlags {
    pub const TYPE_64: Self = Self(0x0000_0001);
    pub const WAIT: Self = Self(0x0000_0002);
}

vk_flags!(ImageViewCreateFlags);
vk_flags!(AccessFlags);
impl AccessFlags {
    pub const SHADER_READ: Self = Self(0x0000_0020);
    pub const SHADER_WRITE: Self = Self(0x0000_0040);
    pub const TRANSFER_READ: Self = Self(0x0000_0800);
    pub const HOST_WRITE: Self = Self(0x0000_4000);
}

vk_flags!(DependencyFlags);
vk_flags!(CommandPoolCreateFlags);
impl CommandPoolCreateFlags {
    pub const RESET_COMMAND_BUFFER: Self = Self(0x0000_0002);
}

vk_flags!(CommandBufferResetFlags);
vk_flags!(CommandBufferUsageFlags);
vk_flags!(ShaderModuleCreateFlags);
vk_flags!(PipelineShaderStageCreateFlags);
vk_flags!(PipelineCreateFlags);
vk_flags!(PipelineLayoutCreateFlags);
vk_flags!(DescriptorSetLayoutCreateFlags);
vk_flags!(DescriptorPoolCreateFlags);
vk_flags!(DescriptorPoolResetFlags);
vk_flags!(SwapchainCreateFlagsKHR);
vk_flags!(SurfaceTransformFlagsKHR);
vk_flags!(CompositeAlphaFlagsKHR);
impl CompositeAlphaFlagsKHR {
    pub const OPAQUE: Self = Self(0x0000_0001);
}

vk_flags!(SampleCountFlags);
impl SampleCountFlags {
    pub const TYPE_1: Self = Self(0x0000_0001);
}

vk_flags!(MetalSurfaceCreateFlagsEXT);

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Extent2D {
    pub width: u32,
    pub height: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Extent3D {
    pub width: u32,
    pub height: u32,
    pub depth: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Offset3D {
    pub x: i32,
    pub y: i32,
    pub z: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct AllocationCallbacks<'a> {
    _private: [u8; 0],
    _marker: PhantomData<&'a ()>,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PhysicalDeviceFeatures {
    _private: [u8; 0],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ApplicationInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub p_application_name: *const c_char,
    pub application_version: u32,
    pub p_engine_name: *const c_char,
    pub engine_version: u32,
    pub api_version: u32,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for ApplicationInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::APPLICATION_INFO,
            p_next: ptr::null(),
            p_application_name: ptr::null(),
            application_version: 0,
            p_engine_name: ptr::null(),
            engine_version: 0,
            api_version: 0,
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct InstanceCreateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: InstanceCreateFlags,
    pub p_application_info: *const ApplicationInfo<'a>,
    pub enabled_layer_count: u32,
    pub pp_enabled_layer_names: *const *const c_char,
    pub enabled_extension_count: u32,
    pub pp_enabled_extension_names: *const *const c_char,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for InstanceCreateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::INSTANCE_CREATE_INFO,
            p_next: ptr::null(),
            flags: InstanceCreateFlags::empty(),
            p_application_info: ptr::null(),
            enabled_layer_count: 0,
            pp_enabled_layer_names: ptr::null(),
            enabled_extension_count: 0,
            pp_enabled_extension_names: ptr::null(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct MemoryHeap {
    pub size: DeviceSize,
    pub flags: MemoryHeapFlags,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct MemoryType {
    pub property_flags: MemoryPropertyFlags,
    pub heap_index: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PhysicalDeviceLimits {
    pub max_image_dimension1_d: u32,
    pub max_image_dimension2_d: u32,
    pub max_image_dimension3_d: u32,
    pub max_image_dimension_cube: u32,
    pub max_image_array_layers: u32,
    pub max_texel_buffer_elements: u32,
    pub max_uniform_buffer_range: u32,
    pub max_storage_buffer_range: u32,
    pub max_push_constants_size: u32,
    pub max_memory_allocation_count: u32,
    pub max_sampler_allocation_count: u32,
    pub buffer_image_granularity: DeviceSize,
    pub sparse_address_space_size: DeviceSize,
    pub max_bound_descriptor_sets: u32,
    pub max_per_stage_descriptor_samplers: u32,
    pub max_per_stage_descriptor_uniform_buffers: u32,
    pub max_per_stage_descriptor_storage_buffers: u32,
    pub max_per_stage_descriptor_sampled_images: u32,
    pub max_per_stage_descriptor_storage_images: u32,
    pub max_per_stage_descriptor_input_attachments: u32,
    pub max_per_stage_resources: u32,
    pub max_descriptor_set_samplers: u32,
    pub max_descriptor_set_uniform_buffers: u32,
    pub max_descriptor_set_uniform_buffers_dynamic: u32,
    pub max_descriptor_set_storage_buffers: u32,
    pub max_descriptor_set_storage_buffers_dynamic: u32,
    pub max_descriptor_set_sampled_images: u32,
    pub max_descriptor_set_storage_images: u32,
    pub max_descriptor_set_input_attachments: u32,
    pub max_vertex_input_attributes: u32,
    pub max_vertex_input_bindings: u32,
    pub max_vertex_input_attribute_offset: u32,
    pub max_vertex_input_binding_stride: u32,
    pub max_vertex_output_components: u32,
    pub max_tessellation_generation_level: u32,
    pub max_tessellation_patch_size: u32,
    pub max_tessellation_control_per_vertex_input_components: u32,
    pub max_tessellation_control_per_vertex_output_components: u32,
    pub max_tessellation_control_per_patch_output_components: u32,
    pub max_tessellation_control_total_output_components: u32,
    pub max_tessellation_evaluation_input_components: u32,
    pub max_tessellation_evaluation_output_components: u32,
    pub max_geometry_shader_invocations: u32,
    pub max_geometry_input_components: u32,
    pub max_geometry_output_components: u32,
    pub max_geometry_output_vertices: u32,
    pub max_geometry_total_output_components: u32,
    pub max_fragment_input_components: u32,
    pub max_fragment_output_attachments: u32,
    pub max_fragment_dual_src_attachments: u32,
    pub max_fragment_combined_output_resources: u32,
    pub max_compute_shared_memory_size: u32,
    pub max_compute_work_group_count: [u32; 3],
    pub max_compute_work_group_invocations: u32,
    pub max_compute_work_group_size: [u32; 3],
    pub sub_pixel_precision_bits: u32,
    pub sub_texel_precision_bits: u32,
    pub mipmap_precision_bits: u32,
    pub max_draw_indexed_index_value: u32,
    pub max_draw_indirect_count: u32,
    pub max_sampler_lod_bias: f32,
    pub max_sampler_anisotropy: f32,
    pub max_viewports: u32,
    pub max_viewport_dimensions: [u32; 2],
    pub viewport_bounds_range: [f32; 2],
    pub viewport_sub_pixel_bits: u32,
    pub min_memory_map_alignment: usize,
    pub min_texel_buffer_offset_alignment: DeviceSize,
    pub min_uniform_buffer_offset_alignment: DeviceSize,
    pub min_storage_buffer_offset_alignment: DeviceSize,
    pub min_texel_offset: i32,
    pub max_texel_offset: u32,
    pub min_texel_gather_offset: i32,
    pub max_texel_gather_offset: u32,
    pub min_interpolation_offset: f32,
    pub max_interpolation_offset: f32,
    pub sub_pixel_interpolation_offset_bits: u32,
    pub max_framebuffer_width: u32,
    pub max_framebuffer_height: u32,
    pub max_framebuffer_layers: u32,
    pub framebuffer_color_sample_counts: SampleCountFlags,
    pub framebuffer_depth_sample_counts: SampleCountFlags,
    pub framebuffer_stencil_sample_counts: SampleCountFlags,
    pub framebuffer_no_attachments_sample_counts: SampleCountFlags,
    pub max_color_attachments: u32,
    pub sampled_image_color_sample_counts: SampleCountFlags,
    pub sampled_image_integer_sample_counts: SampleCountFlags,
    pub sampled_image_depth_sample_counts: SampleCountFlags,
    pub sampled_image_stencil_sample_counts: SampleCountFlags,
    pub storage_image_sample_counts: SampleCountFlags,
    pub max_sample_mask_words: u32,
    pub timestamp_compute_and_graphics: Bool32,
    pub timestamp_period: f32,
    pub max_clip_distances: u32,
    pub max_cull_distances: u32,
    pub max_combined_clip_and_cull_distances: u32,
    pub discrete_queue_priorities: u32,
    pub point_size_range: [f32; 2],
    pub line_width_range: [f32; 2],
    pub point_size_granularity: f32,
    pub line_width_granularity: f32,
    pub strict_lines: Bool32,
    pub standard_sample_locations: Bool32,
    pub optimal_buffer_copy_offset_alignment: DeviceSize,
    pub optimal_buffer_copy_row_pitch_alignment: DeviceSize,
    pub non_coherent_atom_size: DeviceSize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PhysicalDeviceSparseProperties {
    pub residency_standard2_d_block_shape: Bool32,
    pub residency_standard2_d_multisample_block_shape: Bool32,
    pub residency_standard3_d_block_shape: Bool32,
    pub residency_aligned_mip_size: Bool32,
    pub residency_non_resident_strict: Bool32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PhysicalDeviceProperties {
    pub api_version: u32,
    pub driver_version: u32,
    pub vendor_id: u32,
    pub device_id: u32,
    pub device_type: PhysicalDeviceType,
    pub device_name: [c_char; 256],
    pub pipeline_cache_uuid: [u8; 16],
    pub limits: PhysicalDeviceLimits,
    pub sparse_properties: PhysicalDeviceSparseProperties,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PhysicalDeviceMemoryProperties {
    pub memory_type_count: u32,
    pub memory_types: [MemoryType; 32],
    pub memory_heap_count: u32,
    pub memory_heaps: [MemoryHeap; 16],
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct QueueFamilyProperties {
    pub queue_flags: QueueFlags,
    pub queue_count: u32,
    pub timestamp_valid_bits: u32,
    pub min_image_transfer_granularity: Extent3D,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct DeviceQueueCreateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: DeviceQueueCreateFlags,
    pub queue_family_index: u32,
    pub queue_count: u32,
    pub p_queue_priorities: *const f32,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for DeviceQueueCreateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::DEVICE_QUEUE_CREATE_INFO,
            p_next: ptr::null(),
            flags: DeviceQueueCreateFlags::empty(),
            queue_family_index: 0,
            queue_count: 0,
            p_queue_priorities: ptr::null(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct DeviceCreateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: DeviceCreateFlags,
    pub queue_create_info_count: u32,
    pub p_queue_create_infos: *const DeviceQueueCreateInfo<'a>,
    pub enabled_layer_count: u32,
    pub pp_enabled_layer_names: *const *const c_char,
    pub enabled_extension_count: u32,
    pub pp_enabled_extension_names: *const *const c_char,
    pub p_enabled_features: *const PhysicalDeviceFeatures,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for DeviceCreateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::DEVICE_CREATE_INFO,
            p_next: ptr::null(),
            flags: DeviceCreateFlags::empty(),
            queue_create_info_count: 0,
            p_queue_create_infos: ptr::null(),
            enabled_layer_count: 0,
            pp_enabled_layer_names: ptr::null(),
            enabled_extension_count: 0,
            pp_enabled_extension_names: ptr::null(),
            p_enabled_features: ptr::null(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SubmitInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub wait_semaphore_count: u32,
    pub p_wait_semaphores: *const Semaphore,
    pub p_wait_dst_stage_mask: *const PipelineStageFlags,
    pub command_buffer_count: u32,
    pub p_command_buffers: *const CommandBuffer,
    pub signal_semaphore_count: u32,
    pub p_signal_semaphores: *const Semaphore,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for SubmitInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::SUBMIT_INFO,
            p_next: ptr::null(),
            wait_semaphore_count: 0,
            p_wait_semaphores: ptr::null(),
            p_wait_dst_stage_mask: ptr::null(),
            command_buffer_count: 0,
            p_command_buffers: ptr::null(),
            signal_semaphore_count: 0,
            p_signal_semaphores: ptr::null(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct MemoryAllocateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub allocation_size: DeviceSize,
    pub memory_type_index: u32,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for MemoryAllocateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::MEMORY_ALLOCATE_INFO,
            p_next: ptr::null(),
            allocation_size: 0,
            memory_type_index: 0,
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct MemoryRequirements {
    pub size: DeviceSize,
    pub alignment: DeviceSize,
    pub memory_type_bits: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FenceCreateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: FenceCreateFlags,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for FenceCreateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::FENCE_CREATE_INFO,
            p_next: ptr::null(),
            flags: FenceCreateFlags::empty(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SemaphoreCreateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: SemaphoreCreateFlags,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for SemaphoreCreateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::SEMAPHORE_CREATE_INFO,
            p_next: ptr::null(),
            flags: SemaphoreCreateFlags::empty(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct QueryPoolCreateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: QueryPoolCreateFlags,
    pub query_type: QueryType,
    pub query_count: u32,
    pub pipeline_statistics: QueryPipelineStatisticFlags,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for QueryPoolCreateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::QUERY_POOL_CREATE_INFO,
            p_next: ptr::null(),
            flags: QueryPoolCreateFlags::empty(),
            query_type: QueryType::default(),
            query_count: 0,
            pipeline_statistics: QueryPipelineStatisticFlags::empty(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct BufferCreateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: BufferCreateFlags,
    pub size: DeviceSize,
    pub usage: BufferUsageFlags,
    pub sharing_mode: SharingMode,
    pub queue_family_index_count: u32,
    pub p_queue_family_indices: *const u32,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for BufferCreateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::BUFFER_CREATE_INFO,
            p_next: ptr::null(),
            flags: BufferCreateFlags::empty(),
            size: 0,
            usage: BufferUsageFlags::empty(),
            sharing_mode: SharingMode::default(),
            queue_family_index_count: 0,
            p_queue_family_indices: ptr::null(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ImageCreateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: ImageCreateFlags,
    pub image_type: ImageType,
    pub format: Format,
    pub extent: Extent3D,
    pub mip_levels: u32,
    pub array_layers: u32,
    pub samples: SampleCountFlags,
    pub tiling: ImageTiling,
    pub usage: ImageUsageFlags,
    pub sharing_mode: SharingMode,
    pub queue_family_index_count: u32,
    pub p_queue_family_indices: *const u32,
    pub initial_layout: ImageLayout,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for ImageCreateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::IMAGE_CREATE_INFO,
            p_next: ptr::null(),
            flags: ImageCreateFlags::empty(),
            image_type: ImageType::default(),
            format: Format::default(),
            extent: Extent3D::default(),
            mip_levels: 0,
            array_layers: 0,
            samples: SampleCountFlags::empty(),
            tiling: ImageTiling::default(),
            usage: ImageUsageFlags::empty(),
            sharing_mode: SharingMode::default(),
            queue_family_index_count: 0,
            p_queue_family_indices: ptr::null(),
            initial_layout: ImageLayout::default(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct ComponentMapping {
    pub r: ComponentSwizzle,
    pub g: ComponentSwizzle,
    pub b: ComponentSwizzle,
    pub a: ComponentSwizzle,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct ImageSubresourceRange {
    pub aspect_mask: ImageAspectFlags,
    pub base_mip_level: u32,
    pub level_count: u32,
    pub base_array_layer: u32,
    pub layer_count: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct ImageSubresourceLayers {
    pub aspect_mask: ImageAspectFlags,
    pub mip_level: u32,
    pub base_array_layer: u32,
    pub layer_count: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct BufferImageCopy {
    pub buffer_offset: DeviceSize,
    pub buffer_row_length: u32,
    pub buffer_image_height: u32,
    pub image_subresource: ImageSubresourceLayers,
    pub image_offset: Offset3D,
    pub image_extent: Extent3D,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ImageViewCreateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: ImageViewCreateFlags,
    pub image: Image,
    pub view_type: ImageViewType,
    pub format: Format,
    pub components: ComponentMapping,
    pub subresource_range: ImageSubresourceRange,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for ImageViewCreateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::IMAGE_VIEW_CREATE_INFO,
            p_next: ptr::null(),
            flags: ImageViewCreateFlags::empty(),
            image: Image::null(),
            view_type: ImageViewType::default(),
            format: Format::default(),
            components: ComponentMapping::default(),
            subresource_range: ImageSubresourceRange::default(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ShaderModuleCreateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: ShaderModuleCreateFlags,
    pub code_size: usize,
    pub p_code: *const u32,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for ShaderModuleCreateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::SHADER_MODULE_CREATE_INFO,
            p_next: ptr::null(),
            flags: ShaderModuleCreateFlags::empty(),
            code_size: 0,
            p_code: ptr::null(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SpecializationInfo<'a> {
    _private: [u8; 0],
    _marker: PhantomData<&'a ()>,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PipelineShaderStageCreateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: PipelineShaderStageCreateFlags,
    pub stage: ShaderStageFlags,
    pub module: ShaderModule,
    pub p_name: *const c_char,
    pub p_specialization_info: *const SpecializationInfo<'a>,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for PipelineShaderStageCreateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::PIPELINE_SHADER_STAGE_CREATE_INFO,
            p_next: ptr::null(),
            flags: PipelineShaderStageCreateFlags::empty(),
            stage: ShaderStageFlags::empty(),
            module: ShaderModule::null(),
            p_name: ptr::null(),
            p_specialization_info: ptr::null(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ComputePipelineCreateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: PipelineCreateFlags,
    pub stage: PipelineShaderStageCreateInfo<'a>,
    pub layout: PipelineLayout,
    pub base_pipeline_handle: Pipeline,
    pub base_pipeline_index: i32,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for ComputePipelineCreateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::COMPUTE_PIPELINE_CREATE_INFO,
            p_next: ptr::null(),
            flags: PipelineCreateFlags::empty(),
            stage: PipelineShaderStageCreateInfo::default(),
            layout: PipelineLayout::null(),
            base_pipeline_handle: Pipeline::null(),
            base_pipeline_index: 0,
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PushConstantRange {
    pub stage_flags: ShaderStageFlags,
    pub offset: u32,
    pub size: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PipelineLayoutCreateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: PipelineLayoutCreateFlags,
    pub set_layout_count: u32,
    pub p_set_layouts: *const DescriptorSetLayout,
    pub push_constant_range_count: u32,
    pub p_push_constant_ranges: *const PushConstantRange,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for PipelineLayoutCreateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::PIPELINE_LAYOUT_CREATE_INFO,
            p_next: ptr::null(),
            flags: PipelineLayoutCreateFlags::empty(),
            set_layout_count: 0,
            p_set_layouts: ptr::null(),
            push_constant_range_count: 0,
            p_push_constant_ranges: ptr::null(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct DescriptorBufferInfo {
    pub buffer: Buffer,
    pub offset: DeviceSize,
    pub range: DeviceSize,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct DescriptorImageInfo {
    pub sampler: Sampler,
    pub image_view: ImageView,
    pub image_layout: ImageLayout,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct DescriptorPoolSize {
    pub ty: DescriptorType,
    pub descriptor_count: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct DescriptorPoolCreateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: DescriptorPoolCreateFlags,
    pub max_sets: u32,
    pub pool_size_count: u32,
    pub p_pool_sizes: *const DescriptorPoolSize,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for DescriptorPoolCreateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::DESCRIPTOR_POOL_CREATE_INFO,
            p_next: ptr::null(),
            flags: DescriptorPoolCreateFlags::empty(),
            max_sets: 0,
            pool_size_count: 0,
            p_pool_sizes: ptr::null(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct DescriptorSetAllocateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub descriptor_pool: DescriptorPool,
    pub descriptor_set_count: u32,
    pub p_set_layouts: *const DescriptorSetLayout,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for DescriptorSetAllocateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::DESCRIPTOR_SET_ALLOCATE_INFO,
            p_next: ptr::null(),
            descriptor_pool: DescriptorPool::null(),
            descriptor_set_count: 0,
            p_set_layouts: ptr::null(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct DescriptorSetLayoutBinding<'a> {
    pub binding: u32,
    pub descriptor_type: DescriptorType,
    pub descriptor_count: u32,
    pub stage_flags: ShaderStageFlags,
    pub p_immutable_samplers: *const Sampler,
    pub _marker: PhantomData<&'a ()>,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct DescriptorSetLayoutCreateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: DescriptorSetLayoutCreateFlags,
    pub binding_count: u32,
    pub p_bindings: *const DescriptorSetLayoutBinding<'a>,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for DescriptorSetLayoutCreateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            p_next: ptr::null(),
            flags: DescriptorSetLayoutCreateFlags::empty(),
            binding_count: 0,
            p_bindings: ptr::null(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct WriteDescriptorSet<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub dst_set: DescriptorSet,
    pub dst_binding: u32,
    pub dst_array_element: u32,
    pub descriptor_count: u32,
    pub descriptor_type: DescriptorType,
    pub p_image_info: *const DescriptorImageInfo,
    pub p_buffer_info: *const DescriptorBufferInfo,
    pub p_texel_buffer_view: *const BufferView,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for WriteDescriptorSet<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::WRITE_DESCRIPTOR_SET,
            p_next: ptr::null(),
            dst_set: DescriptorSet::null(),
            dst_binding: 0,
            dst_array_element: 0,
            descriptor_count: 0,
            descriptor_type: DescriptorType::default(),
            p_image_info: ptr::null(),
            p_buffer_info: ptr::null(),
            p_texel_buffer_view: ptr::null(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct CopyDescriptorSet<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub src_set: DescriptorSet,
    pub src_binding: u32,
    pub src_array_element: u32,
    pub dst_set: DescriptorSet,
    pub dst_binding: u32,
    pub dst_array_element: u32,
    pub descriptor_count: u32,
    pub _marker: PhantomData<&'a ()>,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct CommandPoolCreateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: CommandPoolCreateFlags,
    pub queue_family_index: u32,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for CommandPoolCreateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::COMMAND_POOL_CREATE_INFO,
            p_next: ptr::null(),
            flags: CommandPoolCreateFlags::empty(),
            queue_family_index: 0,
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct CommandBufferAllocateInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub command_pool: CommandPool,
    pub level: CommandBufferLevel,
    pub command_buffer_count: u32,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for CommandBufferAllocateInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::COMMAND_BUFFER_ALLOCATE_INFO,
            p_next: ptr::null(),
            command_pool: CommandPool::null(),
            level: CommandBufferLevel::default(),
            command_buffer_count: 0,
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct CommandBufferBeginInfo<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: CommandBufferUsageFlags,
    pub p_inheritance_info: *const c_void,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for CommandBufferBeginInfo<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::COMMAND_BUFFER_BEGIN_INFO,
            p_next: ptr::null(),
            flags: CommandBufferUsageFlags::empty(),
            p_inheritance_info: ptr::null(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct MemoryBarrier<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub src_access_mask: AccessFlags,
    pub dst_access_mask: AccessFlags,
    pub _marker: PhantomData<&'a ()>,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct BufferMemoryBarrier<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub src_access_mask: AccessFlags,
    pub dst_access_mask: AccessFlags,
    pub src_queue_family_index: u32,
    pub dst_queue_family_index: u32,
    pub buffer: Buffer,
    pub offset: DeviceSize,
    pub size: DeviceSize,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for BufferMemoryBarrier<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::BUFFER_MEMORY_BARRIER,
            p_next: ptr::null(),
            src_access_mask: AccessFlags::empty(),
            dst_access_mask: AccessFlags::empty(),
            src_queue_family_index: 0,
            dst_queue_family_index: 0,
            buffer: Buffer::null(),
            offset: 0,
            size: 0,
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ImageMemoryBarrier<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub src_access_mask: AccessFlags,
    pub dst_access_mask: AccessFlags,
    pub old_layout: ImageLayout,
    pub new_layout: ImageLayout,
    pub src_queue_family_index: u32,
    pub dst_queue_family_index: u32,
    pub image: Image,
    pub subresource_range: ImageSubresourceRange,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for ImageMemoryBarrier<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::IMAGE_MEMORY_BARRIER,
            p_next: ptr::null(),
            src_access_mask: AccessFlags::empty(),
            dst_access_mask: AccessFlags::empty(),
            old_layout: ImageLayout::default(),
            new_layout: ImageLayout::default(),
            src_queue_family_index: 0,
            dst_queue_family_index: 0,
            image: Image::null(),
            subresource_range: ImageSubresourceRange::default(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct SurfaceCapabilitiesKHR {
    pub min_image_count: u32,
    pub max_image_count: u32,
    pub current_extent: Extent2D,
    pub min_image_extent: Extent2D,
    pub max_image_extent: Extent2D,
    pub max_image_array_layers: u32,
    pub supported_transforms: SurfaceTransformFlagsKHR,
    pub current_transform: SurfaceTransformFlagsKHR,
    pub supported_composite_alpha: CompositeAlphaFlagsKHR,
    pub supported_usage_flags: ImageUsageFlags,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SwapchainCreateInfoKHR<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: SwapchainCreateFlagsKHR,
    pub surface: SurfaceKHR,
    pub min_image_count: u32,
    pub image_format: Format,
    pub image_color_space: ColorSpaceKHR,
    pub image_extent: Extent2D,
    pub image_array_layers: u32,
    pub image_usage: ImageUsageFlags,
    pub image_sharing_mode: SharingMode,
    pub queue_family_index_count: u32,
    pub p_queue_family_indices: *const u32,
    pub pre_transform: SurfaceTransformFlagsKHR,
    pub composite_alpha: CompositeAlphaFlagsKHR,
    pub present_mode: PresentModeKHR,
    pub clipped: Bool32,
    pub old_swapchain: SwapchainKHR,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for SwapchainCreateInfoKHR<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::SWAPCHAIN_CREATE_INFO_KHR,
            p_next: ptr::null(),
            flags: SwapchainCreateFlagsKHR::empty(),
            surface: SurfaceKHR::null(),
            min_image_count: 0,
            image_format: Format::default(),
            image_color_space: ColorSpaceKHR::default(),
            image_extent: Extent2D::default(),
            image_array_layers: 0,
            image_usage: ImageUsageFlags::empty(),
            image_sharing_mode: SharingMode::default(),
            queue_family_index_count: 0,
            p_queue_family_indices: ptr::null(),
            pre_transform: SurfaceTransformFlagsKHR::empty(),
            composite_alpha: CompositeAlphaFlagsKHR::empty(),
            present_mode: PresentModeKHR::default(),
            clipped: FALSE,
            old_swapchain: SwapchainKHR::null(),
            _marker: PhantomData,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PresentInfoKHR<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub wait_semaphore_count: u32,
    pub p_wait_semaphores: *const Semaphore,
    pub swapchain_count: u32,
    pub p_swapchains: *const SwapchainKHR,
    pub p_image_indices: *const u32,
    pub p_results: *mut Result,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for PresentInfoKHR<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::PRESENT_INFO_KHR,
            p_next: ptr::null(),
            wait_semaphore_count: 0,
            p_wait_semaphores: ptr::null(),
            swapchain_count: 0,
            p_swapchains: ptr::null(),
            p_image_indices: ptr::null(),
            p_results: ptr::null_mut(),
            _marker: PhantomData,
        }
    }
}

pub type CAMetalLayer = c_void;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct MetalSurfaceCreateInfoEXT<'a> {
    pub s_type: StructureType,
    pub p_next: *const c_void,
    pub flags: MetalSurfaceCreateFlagsEXT,
    pub p_layer: *const CAMetalLayer,
    pub _marker: PhantomData<&'a ()>,
}

impl Default for MetalSurfaceCreateInfoEXT<'_> {
    fn default() -> Self {
        Self {
            s_type: StructureType::METAL_SURFACE_CREATE_INFO_EXT,
            p_next: ptr::null(),
            flags: MetalSurfaceCreateFlagsEXT::empty(),
            p_layer: ptr::null(),
            _marker: PhantomData,
        }
    }
}

pub mod khr {
    use super::*;

    pub mod surface {
        use super::*;
        pub const NAME: &CStr = c"VK_KHR_surface";
    }

    pub mod swapchain {
        use super::*;
        pub const NAME: &CStr = c"VK_KHR_swapchain";
    }

    pub mod portability_enumeration {
        use super::*;
        pub const NAME: &CStr = c"VK_KHR_portability_enumeration";
    }

    pub mod portability_subset {
        use super::*;
        pub const NAME: &CStr = c"VK_KHR_portability_subset";
    }
}

pub mod ext {
    use super::*;

    pub mod metal_surface {
        use super::*;
        pub const NAME: &CStr = c"VK_EXT_metal_surface";
    }
}

unsafe extern "system" {
    fn vkCreateInstance(
        p_create_info: *const InstanceCreateInfo<'_>,
        p_allocator: *const AllocationCallbacks<'_>,
        p_instance: *mut Instance,
    ) -> Result;
    fn vkEnumeratePhysicalDevices(
        instance: Instance,
        p_count: *mut u32,
        p_devices: *mut PhysicalDevice,
    ) -> Result;
    fn vkGetPhysicalDeviceProperties(
        physical_device: PhysicalDevice,
        p_properties: *mut PhysicalDeviceProperties,
    );
    fn vkGetPhysicalDeviceQueueFamilyProperties(
        physical_device: PhysicalDevice,
        p_count: *mut u32,
        p_properties: *mut QueueFamilyProperties,
    );
    fn vkGetPhysicalDeviceMemoryProperties(
        physical_device: PhysicalDevice,
        p_properties: *mut PhysicalDeviceMemoryProperties,
    );
    fn vkCreateDevice(
        physical_device: PhysicalDevice,
        p_create_info: *const DeviceCreateInfo<'_>,
        p_allocator: *const AllocationCallbacks<'_>,
        p_device: *mut Device,
    ) -> Result;
    fn vkCreateMetalSurfaceEXT(
        instance: Instance,
        p_create_info: *const MetalSurfaceCreateInfoEXT<'_>,
        p_allocator: *const AllocationCallbacks<'_>,
        p_surface: *mut SurfaceKHR,
    ) -> Result;
    fn vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        physical_device: PhysicalDevice,
        surface: SurfaceKHR,
        p_capabilities: *mut SurfaceCapabilitiesKHR,
    ) -> Result;
    fn vkGetDeviceQueue(device: Device, family_index: u32, queue_index: u32, p_queue: *mut Queue);
    fn vkQueueSubmit(
        queue: Queue,
        submit_count: u32,
        p_submits: *const SubmitInfo<'_>,
        fence: Fence,
    ) -> Result;
    fn vkAllocateMemory(
        device: Device,
        p_info: *const MemoryAllocateInfo<'_>,
        p_allocator: *const AllocationCallbacks<'_>,
        p_memory: *mut DeviceMemory,
    ) -> Result;
    fn vkMapMemory(
        device: Device,
        memory: DeviceMemory,
        offset: DeviceSize,
        size: DeviceSize,
        flags: MemoryMapFlags,
        pp_data: *mut *mut c_void,
    ) -> Result;
    fn vkBindBufferMemory(
        device: Device,
        buffer: Buffer,
        memory: DeviceMemory,
        offset: DeviceSize,
    ) -> Result;
    fn vkBindImageMemory(
        device: Device,
        image: Image,
        memory: DeviceMemory,
        offset: DeviceSize,
    ) -> Result;
    fn vkGetBufferMemoryRequirements(
        device: Device,
        buffer: Buffer,
        p_requirements: *mut MemoryRequirements,
    );
    fn vkGetImageMemoryRequirements(
        device: Device,
        image: Image,
        p_requirements: *mut MemoryRequirements,
    );
    fn vkCreateFence(
        device: Device,
        p_info: *const FenceCreateInfo<'_>,
        p_allocator: *const AllocationCallbacks<'_>,
        p_fence: *mut Fence,
    ) -> Result;
    fn vkResetFences(device: Device, fence_count: u32, p_fences: *const Fence) -> Result;
    fn vkWaitForFences(
        device: Device,
        fence_count: u32,
        p_fences: *const Fence,
        wait_all: Bool32,
        timeout: u64,
    ) -> Result;
    fn vkCreateSemaphore(
        device: Device,
        p_info: *const SemaphoreCreateInfo<'_>,
        p_allocator: *const AllocationCallbacks<'_>,
        p_semaphore: *mut Semaphore,
    ) -> Result;
    fn vkCreateQueryPool(
        device: Device,
        p_info: *const QueryPoolCreateInfo<'_>,
        p_allocator: *const AllocationCallbacks<'_>,
        p_pool: *mut QueryPool,
    ) -> Result;
    fn vkGetQueryPoolResults(
        device: Device,
        pool: QueryPool,
        first_query: u32,
        query_count: u32,
        data_size: usize,
        p_data: *mut c_void,
        stride: DeviceSize,
        flags: QueryResultFlags,
    ) -> Result;
    fn vkCreateBuffer(
        device: Device,
        p_info: *const BufferCreateInfo<'_>,
        p_allocator: *const AllocationCallbacks<'_>,
        p_buffer: *mut Buffer,
    ) -> Result;
    fn vkCreateImage(
        device: Device,
        p_info: *const ImageCreateInfo<'_>,
        p_allocator: *const AllocationCallbacks<'_>,
        p_image: *mut Image,
    ) -> Result;
    fn vkCreateImageView(
        device: Device,
        p_info: *const ImageViewCreateInfo<'_>,
        p_allocator: *const AllocationCallbacks<'_>,
        p_view: *mut ImageView,
    ) -> Result;
    fn vkCreateShaderModule(
        device: Device,
        p_info: *const ShaderModuleCreateInfo<'_>,
        p_allocator: *const AllocationCallbacks<'_>,
        p_module: *mut ShaderModule,
    ) -> Result;
    fn vkDestroyShaderModule(
        device: Device,
        module: ShaderModule,
        p_allocator: *const AllocationCallbacks<'_>,
    );
    fn vkCreateComputePipelines(
        device: Device,
        cache: PipelineCache,
        count: u32,
        p_infos: *const ComputePipelineCreateInfo<'_>,
        p_allocator: *const AllocationCallbacks<'_>,
        p_pipelines: *mut Pipeline,
    ) -> Result;
    fn vkCreatePipelineLayout(
        device: Device,
        p_info: *const PipelineLayoutCreateInfo<'_>,
        p_allocator: *const AllocationCallbacks<'_>,
        p_layout: *mut PipelineLayout,
    ) -> Result;
    fn vkCreateDescriptorSetLayout(
        device: Device,
        p_info: *const DescriptorSetLayoutCreateInfo<'_>,
        p_allocator: *const AllocationCallbacks<'_>,
        p_layout: *mut DescriptorSetLayout,
    ) -> Result;
    fn vkCreateDescriptorPool(
        device: Device,
        p_info: *const DescriptorPoolCreateInfo<'_>,
        p_allocator: *const AllocationCallbacks<'_>,
        p_pool: *mut DescriptorPool,
    ) -> Result;
    fn vkAllocateDescriptorSets(
        device: Device,
        p_info: *const DescriptorSetAllocateInfo<'_>,
        p_sets: *mut DescriptorSet,
    ) -> Result;
    fn vkUpdateDescriptorSets(
        device: Device,
        write_count: u32,
        p_writes: *const WriteDescriptorSet<'_>,
        copy_count: u32,
        p_copies: *const CopyDescriptorSet<'_>,
    );
    fn vkCreateCommandPool(
        device: Device,
        p_info: *const CommandPoolCreateInfo<'_>,
        p_allocator: *const AllocationCallbacks<'_>,
        p_pool: *mut CommandPool,
    ) -> Result;
    fn vkAllocateCommandBuffers(
        device: Device,
        p_info: *const CommandBufferAllocateInfo<'_>,
        p_buffers: *mut CommandBuffer,
    ) -> Result;
    fn vkBeginCommandBuffer(
        buffer: CommandBuffer,
        p_info: *const CommandBufferBeginInfo<'_>,
    ) -> Result;
    fn vkEndCommandBuffer(buffer: CommandBuffer) -> Result;
    fn vkResetCommandBuffer(buffer: CommandBuffer, flags: CommandBufferResetFlags) -> Result;
    fn vkCmdBindPipeline(buffer: CommandBuffer, bind_point: PipelineBindPoint, pipeline: Pipeline);
    fn vkCmdBindDescriptorSets(
        buffer: CommandBuffer,
        bind_point: PipelineBindPoint,
        layout: PipelineLayout,
        first_set: u32,
        set_count: u32,
        p_sets: *const DescriptorSet,
        dynamic_offset_count: u32,
        p_dynamic_offsets: *const u32,
    );
    fn vkCmdDispatch(
        buffer: CommandBuffer,
        group_count_x: u32,
        group_count_y: u32,
        group_count_z: u32,
    );
    fn vkCmdCopyImageToBuffer(
        buffer: CommandBuffer,
        src_image: Image,
        src_image_layout: ImageLayout,
        dst_buffer: Buffer,
        region_count: u32,
        p_regions: *const BufferImageCopy,
    );
    fn vkCmdPipelineBarrier(
        buffer: CommandBuffer,
        src_stage_mask: PipelineStageFlags,
        dst_stage_mask: PipelineStageFlags,
        dependency_flags: DependencyFlags,
        memory_barrier_count: u32,
        p_memory_barriers: *const MemoryBarrier<'_>,
        buffer_memory_barrier_count: u32,
        p_buffer_memory_barriers: *const BufferMemoryBarrier<'_>,
        image_memory_barrier_count: u32,
        p_image_memory_barriers: *const ImageMemoryBarrier<'_>,
    );
    fn vkCmdResetQueryPool(
        buffer: CommandBuffer,
        pool: QueryPool,
        first_query: u32,
        query_count: u32,
    );
    fn vkCmdWriteTimestamp(
        buffer: CommandBuffer,
        stage: PipelineStageFlags,
        pool: QueryPool,
        query: u32,
    );
    fn vkCreateSwapchainKHR(
        device: Device,
        p_info: *const SwapchainCreateInfoKHR<'_>,
        p_allocator: *const AllocationCallbacks<'_>,
        p_swapchain: *mut SwapchainKHR,
    ) -> Result;
    fn vkGetSwapchainImagesKHR(
        device: Device,
        swapchain: SwapchainKHR,
        p_count: *mut u32,
        p_images: *mut Image,
    ) -> Result;
    fn vkAcquireNextImageKHR(
        device: Device,
        swapchain: SwapchainKHR,
        timeout: u64,
        semaphore: Semaphore,
        fence: Fence,
        p_image_index: *mut u32,
    ) -> Result;
    fn vkQueuePresentKHR(queue: Queue, p_info: *const PresentInfoKHR<'_>) -> Result;
}

fn callbacks_ptr<'a, 'b>(
    callbacks: Option<&'a AllocationCallbacks<'b>>,
) -> *const AllocationCallbacks<'b> {
    callbacks.map_or(ptr::null(), |callbacks| callbacks)
}

unsafe fn read_into_uninitialized_vector<T>(
    mut f: impl FnMut(*mut u32, *mut T) -> Result,
) -> VkResult<Vec<T>> {
    let mut count = 0u32;
    f(&mut count, ptr::null_mut()).result()?;
    if count == 0 {
        return Ok(Vec::new());
    }

    loop {
        let mut data = Vec::<T>::with_capacity(count as usize);
        let mut actual = count;
        let result = f(&mut actual, data.as_mut_ptr());
        match result {
            Result::SUCCESS => {
                data.set_len(actual as usize);
                return Ok(data);
            }
            Result::INCOMPLETE => {
                count = count.saturating_mul(2).max(actual).max(1);
            }
            err => return Err(err),
        }
    }
}

#[derive(Clone, Copy)]
pub struct Entry;

impl Entry {
    pub fn linked() -> Self {
        Self
    }

    pub unsafe fn create_instance(
        &self,
        info: &InstanceCreateInfo<'_>,
        callbacks: Option<&AllocationCallbacks<'_>>,
    ) -> VkResult<InstanceCommands> {
        let mut instance = MaybeUninit::uninit();
        let handle = vkCreateInstance(info, callbacks_ptr(callbacks), instance.as_mut_ptr())
            .assume_init_on_success(instance)?;
        Ok(InstanceCommands { handle })
    }
}

#[derive(Clone, Copy)]
pub struct InstanceCommands {
    handle: Instance,
}

impl InstanceCommands {
    pub fn handle(&self) -> Instance {
        self.handle
    }

    pub unsafe fn create_device(
        &self,
        physical_device: PhysicalDevice,
        info: &DeviceCreateInfo<'_>,
        callbacks: Option<&AllocationCallbacks<'_>>,
    ) -> VkResult<DeviceCommands> {
        let mut device = MaybeUninit::uninit();
        let handle = vkCreateDevice(
            physical_device,
            info,
            callbacks_ptr(callbacks),
            device.as_mut_ptr(),
        )
        .assume_init_on_success(device)?;
        Ok(DeviceCommands { handle })
    }

    pub unsafe fn enumerate_physical_devices(&self) -> VkResult<Vec<PhysicalDevice>> {
        read_into_uninitialized_vector(|count, data| {
            vkEnumeratePhysicalDevices(self.handle, count, data)
        })
    }

    pub unsafe fn get_physical_device_properties(
        &self,
        physical_device: PhysicalDevice,
    ) -> PhysicalDeviceProperties {
        let mut properties = MaybeUninit::uninit();
        vkGetPhysicalDeviceProperties(physical_device, properties.as_mut_ptr());
        properties.assume_init()
    }

    pub unsafe fn get_physical_device_queue_family_properties(
        &self,
        physical_device: PhysicalDevice,
    ) -> Vec<QueueFamilyProperties> {
        let mut count = 0u32;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &mut count, ptr::null_mut());
        let mut properties = Vec::<QueueFamilyProperties>::with_capacity(count as usize);
        vkGetPhysicalDeviceQueueFamilyProperties(
            physical_device,
            &mut count,
            properties.as_mut_ptr(),
        );
        properties.set_len(count as usize);
        properties
    }

    pub unsafe fn get_physical_device_memory_properties(
        &self,
        physical_device: PhysicalDevice,
    ) -> PhysicalDeviceMemoryProperties {
        let mut properties = MaybeUninit::uninit();
        vkGetPhysicalDeviceMemoryProperties(physical_device, properties.as_mut_ptr());
        properties.assume_init()
    }
}

#[derive(Clone, Copy)]
pub struct DeviceCommands {
    handle: Device,
}

impl DeviceCommands {
    pub fn handle(&self) -> Device {
        self.handle
    }

    pub unsafe fn get_device_queue(&self, family_index: u32, queue_index: u32) -> Queue {
        let mut queue = MaybeUninit::uninit();
        vkGetDeviceQueue(self.handle, family_index, queue_index, queue.as_mut_ptr());
        queue.assume_init()
    }

    pub unsafe fn create_buffer(
        &self,
        info: &BufferCreateInfo<'_>,
        callbacks: Option<&AllocationCallbacks<'_>>,
    ) -> VkResult<Buffer> {
        let mut buffer = MaybeUninit::uninit();
        vkCreateBuffer(
            self.handle,
            info,
            callbacks_ptr(callbacks),
            buffer.as_mut_ptr(),
        )
        .assume_init_on_success(buffer)
    }

    pub unsafe fn create_image(
        &self,
        info: &ImageCreateInfo<'_>,
        callbacks: Option<&AllocationCallbacks<'_>>,
    ) -> VkResult<Image> {
        let mut image = MaybeUninit::uninit();
        vkCreateImage(
            self.handle,
            info,
            callbacks_ptr(callbacks),
            image.as_mut_ptr(),
        )
        .assume_init_on_success(image)
    }

    pub unsafe fn get_buffer_memory_requirements(&self, buffer: Buffer) -> MemoryRequirements {
        let mut requirements = MaybeUninit::uninit();
        vkGetBufferMemoryRequirements(self.handle, buffer, requirements.as_mut_ptr());
        requirements.assume_init()
    }

    pub unsafe fn get_image_memory_requirements(&self, image: Image) -> MemoryRequirements {
        let mut requirements = MaybeUninit::uninit();
        vkGetImageMemoryRequirements(self.handle, image, requirements.as_mut_ptr());
        requirements.assume_init()
    }

    pub unsafe fn allocate_memory(
        &self,
        info: &MemoryAllocateInfo<'_>,
        callbacks: Option<&AllocationCallbacks<'_>>,
    ) -> VkResult<DeviceMemory> {
        let mut memory = MaybeUninit::uninit();
        vkAllocateMemory(
            self.handle,
            info,
            callbacks_ptr(callbacks),
            memory.as_mut_ptr(),
        )
        .assume_init_on_success(memory)
    }

    pub unsafe fn bind_buffer_memory(
        &self,
        buffer: Buffer,
        memory: DeviceMemory,
        offset: DeviceSize,
    ) -> VkResult<()> {
        vkBindBufferMemory(self.handle, buffer, memory, offset).result()
    }

    pub unsafe fn bind_image_memory(
        &self,
        image: Image,
        memory: DeviceMemory,
        offset: DeviceSize,
    ) -> VkResult<()> {
        vkBindImageMemory(self.handle, image, memory, offset).result()
    }

    pub unsafe fn map_memory(
        &self,
        memory: DeviceMemory,
        offset: DeviceSize,
        size: DeviceSize,
        flags: MemoryMapFlags,
    ) -> VkResult<*mut c_void> {
        let mut data = MaybeUninit::uninit();
        vkMapMemory(self.handle, memory, offset, size, flags, data.as_mut_ptr())
            .assume_init_on_success(data)
    }

    pub unsafe fn create_query_pool(
        &self,
        info: &QueryPoolCreateInfo<'_>,
        callbacks: Option<&AllocationCallbacks<'_>>,
    ) -> VkResult<QueryPool> {
        let mut pool = MaybeUninit::uninit();
        vkCreateQueryPool(
            self.handle,
            info,
            callbacks_ptr(callbacks),
            pool.as_mut_ptr(),
        )
        .assume_init_on_success(pool)
    }

    pub unsafe fn get_query_pool_results<T>(
        &self,
        pool: QueryPool,
        first_query: u32,
        data: &mut [T],
        flags: QueryResultFlags,
    ) -> VkResult<()> {
        vkGetQueryPoolResults(
            self.handle,
            pool,
            first_query,
            data.len() as u32,
            size_of_val(data),
            data.as_mut_ptr().cast(),
            size_of::<T>() as DeviceSize,
            flags,
        )
        .result()
    }

    pub unsafe fn create_descriptor_set_layout(
        &self,
        info: &DescriptorSetLayoutCreateInfo<'_>,
        callbacks: Option<&AllocationCallbacks<'_>>,
    ) -> VkResult<DescriptorSetLayout> {
        let mut layout = MaybeUninit::uninit();
        vkCreateDescriptorSetLayout(
            self.handle,
            info,
            callbacks_ptr(callbacks),
            layout.as_mut_ptr(),
        )
        .assume_init_on_success(layout)
    }

    pub unsafe fn create_descriptor_pool(
        &self,
        info: &DescriptorPoolCreateInfo<'_>,
        callbacks: Option<&AllocationCallbacks<'_>>,
    ) -> VkResult<DescriptorPool> {
        let mut pool = MaybeUninit::uninit();
        vkCreateDescriptorPool(
            self.handle,
            info,
            callbacks_ptr(callbacks),
            pool.as_mut_ptr(),
        )
        .assume_init_on_success(pool)
    }

    pub unsafe fn allocate_descriptor_sets(
        &self,
        info: &DescriptorSetAllocateInfo<'_>,
    ) -> VkResult<Vec<DescriptorSet>> {
        let mut sets = Vec::<DescriptorSet>::with_capacity(info.descriptor_set_count as usize);
        vkAllocateDescriptorSets(self.handle, info, sets.as_mut_ptr()).result()?;
        sets.set_len(info.descriptor_set_count as usize);
        Ok(sets)
    }

    pub unsafe fn create_pipeline_layout(
        &self,
        info: &PipelineLayoutCreateInfo<'_>,
        callbacks: Option<&AllocationCallbacks<'_>>,
    ) -> VkResult<PipelineLayout> {
        let mut layout = MaybeUninit::uninit();
        vkCreatePipelineLayout(
            self.handle,
            info,
            callbacks_ptr(callbacks),
            layout.as_mut_ptr(),
        )
        .assume_init_on_success(layout)
    }

    pub unsafe fn create_shader_module(
        &self,
        info: &ShaderModuleCreateInfo<'_>,
        callbacks: Option<&AllocationCallbacks<'_>>,
    ) -> VkResult<ShaderModule> {
        let mut module = MaybeUninit::uninit();
        vkCreateShaderModule(
            self.handle,
            info,
            callbacks_ptr(callbacks),
            module.as_mut_ptr(),
        )
        .assume_init_on_success(module)
    }

    pub unsafe fn create_compute_pipelines(
        &self,
        cache: PipelineCache,
        infos: &[ComputePipelineCreateInfo<'_>],
        callbacks: Option<&AllocationCallbacks<'_>>,
    ) -> core::result::Result<Vec<Pipeline>, (Vec<Pipeline>, Result)> {
        let mut pipelines = Vec::<Pipeline>::with_capacity(infos.len());
        let result = vkCreateComputePipelines(
            self.handle,
            cache,
            infos.len() as u32,
            infos.as_ptr(),
            callbacks_ptr(callbacks),
            pipelines.as_mut_ptr(),
        );
        pipelines.set_len(infos.len());
        if result == Result::SUCCESS {
            Ok(pipelines)
        } else {
            Err((pipelines, result))
        }
    }

    pub unsafe fn destroy_shader_module(
        &self,
        module: ShaderModule,
        callbacks: Option<&AllocationCallbacks<'_>>,
    ) {
        vkDestroyShaderModule(self.handle, module, callbacks_ptr(callbacks));
    }

    pub unsafe fn create_command_pool(
        &self,
        info: &CommandPoolCreateInfo<'_>,
        callbacks: Option<&AllocationCallbacks<'_>>,
    ) -> VkResult<CommandPool> {
        let mut pool = MaybeUninit::uninit();
        vkCreateCommandPool(
            self.handle,
            info,
            callbacks_ptr(callbacks),
            pool.as_mut_ptr(),
        )
        .assume_init_on_success(pool)
    }

    pub unsafe fn allocate_command_buffers(
        &self,
        info: &CommandBufferAllocateInfo<'_>,
    ) -> VkResult<Vec<CommandBuffer>> {
        let mut buffers = Vec::<CommandBuffer>::with_capacity(info.command_buffer_count as usize);
        vkAllocateCommandBuffers(self.handle, info, buffers.as_mut_ptr()).result()?;
        buffers.set_len(info.command_buffer_count as usize);
        Ok(buffers)
    }

    pub unsafe fn create_image_view(
        &self,
        info: &ImageViewCreateInfo<'_>,
        callbacks: Option<&AllocationCallbacks<'_>>,
    ) -> VkResult<ImageView> {
        let mut view = MaybeUninit::uninit();
        vkCreateImageView(
            self.handle,
            info,
            callbacks_ptr(callbacks),
            view.as_mut_ptr(),
        )
        .assume_init_on_success(view)
    }

    pub unsafe fn update_descriptor_sets(
        &self,
        writes: &[WriteDescriptorSet<'_>],
        copies: &[CopyDescriptorSet<'_>],
    ) {
        vkUpdateDescriptorSets(
            self.handle,
            writes.len() as u32,
            writes.as_ptr(),
            copies.len() as u32,
            copies.as_ptr(),
        );
    }

    pub unsafe fn begin_command_buffer(
        &self,
        buffer: CommandBuffer,
        info: &CommandBufferBeginInfo<'_>,
    ) -> VkResult<()> {
        vkBeginCommandBuffer(buffer, info).result()
    }

    pub unsafe fn end_command_buffer(&self, buffer: CommandBuffer) -> VkResult<()> {
        vkEndCommandBuffer(buffer).result()
    }

    pub unsafe fn reset_command_buffer(
        &self,
        buffer: CommandBuffer,
        flags: CommandBufferResetFlags,
    ) -> VkResult<()> {
        vkResetCommandBuffer(buffer, flags).result()
    }

    pub unsafe fn cmd_reset_query_pool(
        &self,
        buffer: CommandBuffer,
        pool: QueryPool,
        first_query: u32,
        query_count: u32,
    ) {
        vkCmdResetQueryPool(buffer, pool, first_query, query_count);
    }

    pub unsafe fn cmd_pipeline_barrier(
        &self,
        buffer: CommandBuffer,
        src_stage_mask: PipelineStageFlags,
        dst_stage_mask: PipelineStageFlags,
        dependency_flags: DependencyFlags,
        memory_barriers: &[MemoryBarrier<'_>],
        buffer_memory_barriers: &[BufferMemoryBarrier<'_>],
        image_memory_barriers: &[ImageMemoryBarrier<'_>],
    ) {
        vkCmdPipelineBarrier(
            buffer,
            src_stage_mask,
            dst_stage_mask,
            dependency_flags,
            memory_barriers.len() as u32,
            memory_barriers.as_ptr(),
            buffer_memory_barriers.len() as u32,
            buffer_memory_barriers.as_ptr(),
            image_memory_barriers.len() as u32,
            image_memory_barriers.as_ptr(),
        );
    }

    pub unsafe fn cmd_write_timestamp(
        &self,
        buffer: CommandBuffer,
        stage: PipelineStageFlags,
        pool: QueryPool,
        query: u32,
    ) {
        vkCmdWriteTimestamp(buffer, stage, pool, query);
    }

    pub unsafe fn cmd_bind_pipeline(
        &self,
        buffer: CommandBuffer,
        bind_point: PipelineBindPoint,
        pipeline: Pipeline,
    ) {
        vkCmdBindPipeline(buffer, bind_point, pipeline);
    }

    pub unsafe fn cmd_bind_descriptor_sets(
        &self,
        buffer: CommandBuffer,
        bind_point: PipelineBindPoint,
        layout: PipelineLayout,
        first_set: u32,
        sets: &[DescriptorSet],
        dynamic_offsets: &[u32],
    ) {
        vkCmdBindDescriptorSets(
            buffer,
            bind_point,
            layout,
            first_set,
            sets.len() as u32,
            sets.as_ptr(),
            dynamic_offsets.len() as u32,
            dynamic_offsets.as_ptr(),
        );
    }

    pub unsafe fn cmd_dispatch(&self, buffer: CommandBuffer, x: u32, y: u32, z: u32) {
        vkCmdDispatch(buffer, x, y, z);
    }

    pub unsafe fn cmd_copy_image_to_buffer(
        &self,
        buffer: CommandBuffer,
        src_image: Image,
        src_image_layout: ImageLayout,
        dst_buffer: Buffer,
        regions: &[BufferImageCopy],
    ) {
        vkCmdCopyImageToBuffer(
            buffer,
            src_image,
            src_image_layout,
            dst_buffer,
            regions.len() as u32,
            regions.as_ptr(),
        );
    }

    pub unsafe fn create_semaphore(
        &self,
        info: &SemaphoreCreateInfo<'_>,
        callbacks: Option<&AllocationCallbacks<'_>>,
    ) -> VkResult<Semaphore> {
        let mut semaphore = MaybeUninit::uninit();
        vkCreateSemaphore(
            self.handle,
            info,
            callbacks_ptr(callbacks),
            semaphore.as_mut_ptr(),
        )
        .assume_init_on_success(semaphore)
    }

    pub unsafe fn create_fence(
        &self,
        info: &FenceCreateInfo<'_>,
        callbacks: Option<&AllocationCallbacks<'_>>,
    ) -> VkResult<Fence> {
        let mut fence = MaybeUninit::uninit();
        vkCreateFence(
            self.handle,
            info,
            callbacks_ptr(callbacks),
            fence.as_mut_ptr(),
        )
        .assume_init_on_success(fence)
    }

    pub unsafe fn wait_for_fences(
        &self,
        fences: &[Fence],
        wait_all: bool,
        timeout: u64,
    ) -> VkResult<()> {
        vkWaitForFences(
            self.handle,
            fences.len() as u32,
            fences.as_ptr(),
            Bool32::from(wait_all),
            timeout,
        )
        .result()
    }

    pub unsafe fn reset_fences(&self, fences: &[Fence]) -> VkResult<()> {
        vkResetFences(self.handle, fences.len() as u32, fences.as_ptr()).result()
    }

    pub unsafe fn queue_submit(
        &self,
        queue: Queue,
        submits: &[SubmitInfo<'_>],
        fence: Fence,
    ) -> VkResult<()> {
        vkQueueSubmit(queue, submits.len() as u32, submits.as_ptr(), fence).result()
    }
}

#[derive(Clone, Copy)]
pub struct SurfaceLoader;

impl SurfaceLoader {
    pub fn new(_instance: &InstanceCommands) -> Self {
        Self
    }

    pub unsafe fn get_physical_device_surface_capabilities(
        &self,
        physical_device: PhysicalDevice,
        surface: SurfaceKHR,
    ) -> VkResult<SurfaceCapabilitiesKHR> {
        let mut capabilities = MaybeUninit::uninit();
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            physical_device,
            surface,
            capabilities.as_mut_ptr(),
        )
        .assume_init_on_success(capabilities)
    }
}

#[derive(Clone, Copy)]
pub struct MetalSurfaceLoader {
    handle: Instance,
}

impl MetalSurfaceLoader {
    pub fn new(instance: &InstanceCommands) -> Self {
        Self {
            handle: instance.handle,
        }
    }

    pub unsafe fn create_metal_surface(
        &self,
        info: &MetalSurfaceCreateInfoEXT<'_>,
        callbacks: Option<&AllocationCallbacks<'_>>,
    ) -> VkResult<SurfaceKHR> {
        let mut surface = MaybeUninit::uninit();
        vkCreateMetalSurfaceEXT(
            self.handle,
            info,
            callbacks_ptr(callbacks),
            surface.as_mut_ptr(),
        )
        .assume_init_on_success(surface)
    }
}

#[derive(Clone, Copy)]
pub struct SwapchainLoader {
    handle: Device,
}

impl SwapchainLoader {
    pub fn new(device: &DeviceCommands) -> Self {
        Self {
            handle: device.handle,
        }
    }

    pub unsafe fn create_swapchain(
        &self,
        info: &SwapchainCreateInfoKHR<'_>,
        callbacks: Option<&AllocationCallbacks<'_>>,
    ) -> VkResult<SwapchainKHR> {
        let mut swapchain = MaybeUninit::uninit();
        vkCreateSwapchainKHR(
            self.handle,
            info,
            callbacks_ptr(callbacks),
            swapchain.as_mut_ptr(),
        )
        .assume_init_on_success(swapchain)
    }

    pub unsafe fn get_swapchain_images(&self, swapchain: SwapchainKHR) -> VkResult<Vec<Image>> {
        read_into_uninitialized_vector(|count, data| {
            vkGetSwapchainImagesKHR(self.handle, swapchain, count, data)
        })
    }

    pub unsafe fn acquire_next_image(
        &self,
        swapchain: SwapchainKHR,
        timeout: u64,
        semaphore: Semaphore,
        fence: Fence,
    ) -> VkResult<(u32, bool)> {
        let mut index = MaybeUninit::uninit();
        match vkAcquireNextImageKHR(
            self.handle,
            swapchain,
            timeout,
            semaphore,
            fence,
            index.as_mut_ptr(),
        ) {
            Result::SUCCESS => Ok((index.assume_init(), false)),
            Result::SUBOPTIMAL_KHR => Ok((index.assume_init(), true)),
            err => Err(err),
        }
    }

    pub unsafe fn queue_present(&self, queue: Queue, info: &PresentInfoKHR<'_>) -> VkResult<bool> {
        match vkQueuePresentKHR(queue, info) {
            Result::SUCCESS => Ok(false),
            Result::SUBOPTIMAL_KHR => Ok(true),
            err => Err(err),
        }
    }
}
