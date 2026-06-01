#if defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#else
#error Unsupported platform
#endif
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "trace_comp_spv.h"

#define RTR_MAX_SWAP_IMAGES 3u
#define RTR_TILE_SIZE 8u
#define RTR_MEMORY_HEADER_WORDS 16u
#define RTR_HISTORY_WORDS_PER_PIXEL 15u
#define RTR_HISTORY_PAGE_COUNT 2u
#define RTR_MEMORY_MAGIC 0x30525452u
#define RTR_TIMING_WINDOW 100u

void rtrWindowMouse(float *x, float *y);

enum {
    RTR_MEMORY_MAGIC_WORD = 0,
    RTR_MEMORY_VERSION_WORD = 1,
    RTR_MEMORY_WIDTH_WORD = 2,
    RTR_MEMORY_HEIGHT_WORD = 3,
    RTR_MEMORY_FRAME_WORD = 4,
    RTR_MEMORY_MOUSE_X_WORD = 5,
    RTR_MEMORY_MOUSE_Y_WORD = 6,
    RTR_MEMORY_TIME_WORD = 7,
    RTR_MEMORY_PREV_TIME_WORD = 8,
    RTR_MEMORY_PREV_MOUSE_X_WORD = 9,
    RTR_MEMORY_PREV_MOUSE_Y_WORD = 10,
};

static const char *const rtrInstanceExts[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_EXT_METAL_SURFACE_EXTENSION_NAME,
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
};

static const VkInstanceCreateFlags rtrInstanceFlags =
    VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

static const char *const rtrDeviceExts[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME,
};

static VkInstance rtrInstance = VK_NULL_HANDLE;
static VkSurfaceKHR rtrSurface = VK_NULL_HANDLE;
static VkPhysicalDevice rtrPhysicalDevice = VK_NULL_HANDLE;
static VkDevice rtrDevice = VK_NULL_HANDLE;
static VkQueue rtrQueue = VK_NULL_HANDLE;
static VkSwapchainKHR rtrSwapchain = VK_NULL_HANDLE;
static VkExtent2D rtrSwapExtent = {0u, 0u};
static VkImage rtrSwapImages[RTR_MAX_SWAP_IMAGES];
static VkImageView rtrSwapImageViews[RTR_MAX_SWAP_IMAGES];
static VkDescriptorSetLayout rtrDescriptorSetLayout = VK_NULL_HANDLE;
static VkDescriptorPool rtrDescriptorPool = VK_NULL_HANDLE;
static VkDescriptorSet rtrDescriptorSets[RTR_MAX_SWAP_IMAGES];
static VkPipelineLayout rtrPipelineLayout = VK_NULL_HANDLE;
static VkPipeline rtrPipeline = VK_NULL_HANDLE;
static VkCommandPool rtrCommandPool = VK_NULL_HANDLE;
static VkCommandBuffer rtrCommandBuffers[RTR_MAX_SWAP_IMAGES];
static VkSemaphore rtrImageAvailableSemaphore = VK_NULL_HANDLE;
static VkSemaphore rtrRenderFinishedSemaphore = VK_NULL_HANDLE;
static VkFence rtrInFlightFence = VK_NULL_HANDLE;
static VkQueryPool rtrTimingQueryPool = VK_NULL_HANDLE;
static VkBuffer rtrMemoryBuffer = VK_NULL_HANDLE;
static VkDeviceMemory rtrMemoryBufferMemory = VK_NULL_HANDLE;
static uint32_t *rtrMemoryWords = NULL;
static uint32_t rtrFrameIndex = 0u;
static uint32_t rtrTimingSupported = 0u;
static uint32_t rtrTimingPending = 0u;
static uint32_t rtrTimingImageIndex = 0u;
static uint32_t rtrTimingFrameIndex = 0u;
static uint32_t rtrTimingWindowFirstFrame = 0u;
static uint32_t rtrTimingSampleCount = 0u;
static uint32_t rtrTimestampValidBits = 0u;
static float rtrTimestampPeriod = 1.0f;
static double rtrTimingSamples[RTR_TIMING_WINDOW];
static struct timespec rtrStartTime;

static uint32_t rtrF32Word(float value)
{
    uint32_t word = 0u;
    memcpy(&word, &value, sizeof(word));
    return word;
}

static uint32_t rtrFindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(rtrPhysicalDevice, &props);

    for (uint32_t i = 0u; i < props.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) &&
            ((props.memoryTypes[i].propertyFlags & flags) == flags)) {
            return i;
        }
    }

    return UINT32_MAX;
}

static float rtrElapsedSeconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    const double seconds = (double)(now.tv_sec - rtrStartTime.tv_sec);
    const double nanos = (double)(now.tv_nsec - rtrStartTime.tv_nsec) / 1000000000.0;
    return (float)(seconds + nanos);
}

static int rtrCreateMemoryBuffer(void)
{
    const VkDeviceSize historyWords =
        (VkDeviceSize)rtrSwapExtent.width *
        (VkDeviceSize)rtrSwapExtent.height *
        (VkDeviceSize)RTR_HISTORY_WORDS_PER_PIXEL *
        (VkDeviceSize)RTR_HISTORY_PAGE_COUNT;
    const VkDeviceSize rtrMemorySize =
        ((VkDeviceSize)RTR_MEMORY_HEADER_WORDS + historyWords) *
        (VkDeviceSize)sizeof(uint32_t);
    if (vkCreateBuffer(rtrDevice, &(VkBufferCreateInfo){
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = rtrMemorySize,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    }, NULL, &rtrMemoryBuffer) != VK_SUCCESS) return 1;

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(rtrDevice, rtrMemoryBuffer, &requirements);
    uint32_t memoryType = rtrFindMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memoryType == UINT32_MAX) return 1;

    if (vkAllocateMemory(rtrDevice, &(VkMemoryAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memoryType,
    }, NULL, &rtrMemoryBufferMemory) != VK_SUCCESS) return 1;

    vkBindBufferMemory(rtrDevice, rtrMemoryBuffer, rtrMemoryBufferMemory, 0u);
    if (vkMapMemory(rtrDevice, rtrMemoryBufferMemory, 0u, rtrMemorySize, 0u,
                    (void **)&rtrMemoryWords) != VK_SUCCESS) return 1;

    memset(rtrMemoryWords, 0, (size_t)rtrMemorySize);
    rtrMemoryWords[RTR_MEMORY_MAGIC_WORD] = RTR_MEMORY_MAGIC;
    rtrMemoryWords[RTR_MEMORY_VERSION_WORD] = 1u;
    rtrMemoryWords[RTR_MEMORY_WIDTH_WORD] = rtrSwapExtent.width;
    rtrMemoryWords[RTR_MEMORY_HEIGHT_WORD] = rtrSwapExtent.height;
    rtrMemoryWords[RTR_MEMORY_MOUSE_X_WORD] = rtrF32Word(-1.0f);
    rtrMemoryWords[RTR_MEMORY_MOUSE_Y_WORD] = rtrF32Word(-1.0f);
    rtrMemoryWords[RTR_MEMORY_PREV_MOUSE_X_WORD] = rtrF32Word(-1.0f);
    rtrMemoryWords[RTR_MEMORY_PREV_MOUSE_Y_WORD] = rtrF32Word(-1.0f);

    return 0;
}

static void rtrUpdateMemory(void)
{
    const uint32_t prevTimeWord = rtrMemoryWords[RTR_MEMORY_TIME_WORD];
    const uint32_t prevMouseXWord = rtrMemoryWords[RTR_MEMORY_MOUSE_X_WORD];
    const uint32_t prevMouseYWord = rtrMemoryWords[RTR_MEMORY_MOUSE_Y_WORD];
    float mouseX = -1.0f;
    float mouseY = -1.0f;
    rtrWindowMouse(&mouseX, &mouseY);

    rtrMemoryWords[RTR_MEMORY_WIDTH_WORD] = rtrSwapExtent.width;
    rtrMemoryWords[RTR_MEMORY_HEIGHT_WORD] = rtrSwapExtent.height;
    rtrMemoryWords[RTR_MEMORY_FRAME_WORD] = rtrFrameIndex;
    rtrMemoryWords[RTR_MEMORY_MOUSE_X_WORD] = rtrF32Word(mouseX);
    rtrMemoryWords[RTR_MEMORY_MOUSE_Y_WORD] = rtrF32Word(mouseY);
    rtrMemoryWords[RTR_MEMORY_TIME_WORD] = rtrF32Word(rtrElapsedSeconds());
    rtrMemoryWords[RTR_MEMORY_PREV_TIME_WORD] = prevTimeWord;
    rtrMemoryWords[RTR_MEMORY_PREV_MOUSE_X_WORD] = prevMouseXWord;
    rtrMemoryWords[RTR_MEMORY_PREV_MOUSE_Y_WORD] = prevMouseYWord;
}

static int rtrCreateTimingQueryPool(void)
{
    if (!rtrTimingSupported) return 0;

    if (vkCreateQueryPool(rtrDevice, &(VkQueryPoolCreateInfo){
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = RTR_MAX_SWAP_IMAGES * 2u,
    }, NULL, &rtrTimingQueryPool) != VK_SUCCESS) {
        rtrTimingSupported = 0u;
    }

    return 0;
}

static void rtrSortTimingSamples(double *samples, uint32_t count)
{
    for (uint32_t i = 1u; i < count; i++) {
        double value = samples[i];
        uint32_t j = i;
        while (j > 0u && samples[j - 1u] > value) {
            samples[j] = samples[j - 1u];
            j--;
        }
        samples[j] = value;
    }
}

static void rtrPushGpuTiming(double gpuMs)
{
    if (rtrTimingSampleCount == 0u) {
        rtrTimingWindowFirstFrame = rtrTimingFrameIndex;
    }

    rtrTimingSamples[rtrTimingSampleCount++] = gpuMs;
    if (rtrTimingSampleCount < RTR_TIMING_WINDOW) return;

    double sorted[RTR_TIMING_WINDOW];
    double sum = 0.0;
    for (uint32_t i = 0u; i < RTR_TIMING_WINDOW; i++) {
        sorted[i] = rtrTimingSamples[i];
        sum += rtrTimingSamples[i];
    }

    rtrSortTimingSamples(sorted, RTR_TIMING_WINDOW);

    const double avg = sum / (double)RTR_TIMING_WINDOW;
    double variance = 0.0;
    for (uint32_t i = 0u; i < RTR_TIMING_WINDOW; i++) {
        const double delta = rtrTimingSamples[i] - avg;
        variance += delta * delta;
    }
    variance /= (double)RTR_TIMING_WINDOW;

    printf("gpu[%u] frames %u-%u %ux%u avg %.3f ms min %.3f p01 %.3f p99 %.3f max %.3f var %.6f ms^2\n",
           RTR_TIMING_WINDOW,
           rtrTimingWindowFirstFrame,
           rtrTimingFrameIndex,
           rtrSwapExtent.width,
           rtrSwapExtent.height,
           avg,
           sorted[0],
           sorted[0],
           sorted[RTR_TIMING_WINDOW - 2u],
           sorted[RTR_TIMING_WINDOW - 1u],
           variance);
    fflush(stdout);
    rtrTimingSampleCount = 0u;
}

static void rtrReportGpuTiming(void)
{
    if (!(rtrTimingSupported && rtrTimingPending)) return;

    uint64_t timestamp[2] = {0u, 0u};
    const uint32_t query = rtrTimingImageIndex * 2u;
    VkResult result = vkGetQueryPoolResults(
        rtrDevice,
        rtrTimingQueryPool,
        query,
        2u,
        sizeof(timestamp),
        timestamp,
        sizeof(timestamp[0]),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    if (result != VK_SUCCESS) return;

    uint64_t ticks = timestamp[1] - timestamp[0];
    if (rtrTimestampValidBits < 64u) {
        const uint64_t mask = (1ull << rtrTimestampValidBits) - 1ull;
        ticks = (timestamp[1] - timestamp[0]) & mask;
    }

    const double gpuMs = (double)ticks * (double)rtrTimestampPeriod / 1000000.0;
    rtrPushGpuTiming(gpuMs);
    rtrTimingPending = 0u;
}

int rtrVulkanInit(void *windowSurface)
{
    clock_gettime(CLOCK_MONOTONIC, &rtrStartTime);

    vkCreateInstance(&(VkInstanceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags = rtrInstanceFlags,
        .pApplicationInfo = &(VkApplicationInfo){
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "realtimerays",
            .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .pEngineName = "realtimerays",
            .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .apiVersion = VK_API_VERSION_1_3,
        },
        .enabledExtensionCount =
            (uint32_t)(sizeof(rtrInstanceExts) / sizeof(*rtrInstanceExts)),
        .ppEnabledExtensionNames = rtrInstanceExts,
    }, NULL, &rtrInstance);

    vkCreateMetalSurfaceEXT(rtrInstance, &(VkMetalSurfaceCreateInfoEXT){
        .sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
        .pLayer = windowSurface,
    }, NULL, &rtrSurface);

    uint32_t deviceCount = 1u;
    vkEnumeratePhysicalDevices(rtrInstance, &deviceCount, &rtrPhysicalDevice);

    VkPhysicalDeviceProperties deviceProps;
    vkGetPhysicalDeviceProperties(rtrPhysicalDevice, &deviceProps);
    uint32_t queueFamilyCount = 1u;
    VkQueueFamilyProperties queueFamilyProps;
    vkGetPhysicalDeviceQueueFamilyProperties(
        rtrPhysicalDevice, &queueFamilyCount, &queueFamilyProps);
    rtrTimestampPeriod = deviceProps.limits.timestampPeriod;
    rtrTimestampValidBits = queueFamilyProps.timestampValidBits;
    rtrTimingSupported =
        (uint32_t)(deviceProps.limits.timestampComputeAndGraphics &&
                   rtrTimestampValidBits > 0u &&
                   rtrTimestampPeriod > 0.0f);

    float priority = 1.0f;
    vkCreateDevice(rtrPhysicalDevice, &(VkDeviceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1u,
        .pQueueCreateInfos = &(VkDeviceQueueCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = 0u,
            .queueCount = 1u,
            .pQueuePriorities = &priority,
        },
        .enabledExtensionCount =
            (uint32_t)(sizeof(rtrDeviceExts) / sizeof(*rtrDeviceExts)),
        .ppEnabledExtensionNames = rtrDeviceExts,
    }, NULL, &rtrDevice);

    vkGetDeviceQueue(rtrDevice, 0u, 0u, &rtrQueue);

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(rtrPhysicalDevice, rtrSurface, &caps);
    rtrSwapExtent = caps.currentExtent;
    uint32_t swapchainMinImageCount = RTR_MAX_SWAP_IMAGES;
    if (swapchainMinImageCount < caps.minImageCount) swapchainMinImageCount = caps.minImageCount;
    if ((caps.maxImageCount != 0u) && (swapchainMinImageCount > caps.maxImageCount)) {
        swapchainMinImageCount = caps.maxImageCount;
    }

    vkCreateSwapchainKHR(rtrDevice, &(VkSwapchainCreateInfoKHR){
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = rtrSurface,
        .minImageCount = swapchainMinImageCount,
        .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = rtrSwapExtent,
        .imageArrayLayers = 1u,
        .imageUsage = VK_IMAGE_USAGE_STORAGE_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    }, NULL, &rtrSwapchain);

    uint32_t swapImageCount = 0u;
    vkGetSwapchainImagesKHR(rtrDevice, rtrSwapchain, &swapImageCount, NULL);
    if ((swapImageCount < 2u) || (swapImageCount > RTR_MAX_SWAP_IMAGES)) return 1;
    vkGetSwapchainImagesKHR(rtrDevice, rtrSwapchain, &swapImageCount, rtrSwapImages);

    if (rtrCreateMemoryBuffer()) return 1;
    if (rtrCreateTimingQueryPool()) return 1;

    const VkDescriptorSetLayoutBinding bindings[] = {
        {
            .binding = 0u,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1u,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 1u,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1u,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };

    vkCreateDescriptorSetLayout(rtrDevice, &(VkDescriptorSetLayoutCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2u,
        .pBindings = bindings,
    }, NULL, &rtrDescriptorSetLayout);

    const VkDescriptorPoolSize poolSizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = RTR_MAX_SWAP_IMAGES,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = RTR_MAX_SWAP_IMAGES,
        },
    };

    vkCreateDescriptorPool(rtrDevice, &(VkDescriptorPoolCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = RTR_MAX_SWAP_IMAGES,
        .poolSizeCount = 2u,
        .pPoolSizes = poolSizes,
    }, NULL, &rtrDescriptorPool);

    VkDescriptorSetLayout setLayouts[RTR_MAX_SWAP_IMAGES] = {
        rtrDescriptorSetLayout,
        rtrDescriptorSetLayout,
        rtrDescriptorSetLayout,
    };
    vkAllocateDescriptorSets(rtrDevice, &(VkDescriptorSetAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = rtrDescriptorPool,
        .descriptorSetCount = RTR_MAX_SWAP_IMAGES,
        .pSetLayouts = setLayouts,
    }, rtrDescriptorSets);

    vkCreatePipelineLayout(rtrDevice, &(VkPipelineLayoutCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1u,
        .pSetLayouts = &rtrDescriptorSetLayout,
    }, NULL, &rtrPipelineLayout);

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    vkCreateShaderModule(rtrDevice, &(VkShaderModuleCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = traceCompSpv_len,
        .pCode = (const uint32_t *)(const void *)traceCompSpv,
    }, NULL, &shaderModule);

    vkCreateComputePipelines(rtrDevice, VK_NULL_HANDLE, 1u, &(VkComputePipelineCreateInfo){
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shaderModule,
            .pName = "main",
        },
        .layout = rtrPipelineLayout,
        .basePipelineIndex = -1,
    }, NULL, &rtrPipeline);

    vkDestroyShaderModule(rtrDevice, shaderModule, NULL);

    vkCreateCommandPool(rtrDevice, &(VkCommandPoolCreateInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0u,
    }, NULL, &rtrCommandPool);

    vkAllocateCommandBuffers(rtrDevice, &(VkCommandBufferAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = rtrCommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = swapImageCount,
    }, rtrCommandBuffers);

    VkImageSubresourceRange imageRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1u,
        .layerCount = 1u,
    };

    for (uint32_t i = 0u; i < swapImageCount; i++)
    {
        vkCreateImageView(rtrDevice, &(VkImageViewCreateInfo){
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = rtrSwapImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1u,
                .layerCount = 1u,
            },
        }, NULL, &rtrSwapImageViews[i]);

        const VkWriteDescriptorSet writes[] = {
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = rtrDescriptorSets[i],
                .dstBinding = 0u,
                .descriptorCount = 1u,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo = &(VkDescriptorImageInfo){
                    .imageView = rtrSwapImageViews[i],
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                },
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = rtrDescriptorSets[i],
                .dstBinding = 1u,
                .descriptorCount = 1u,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &(VkDescriptorBufferInfo){
                    .buffer = rtrMemoryBuffer,
                    .offset = 0u,
                    .range = VK_WHOLE_SIZE,
                },
            },
        };
        vkUpdateDescriptorSets(rtrDevice, 2u, writes, 0u, NULL);

        vkBeginCommandBuffer(rtrCommandBuffers[i], &(VkCommandBufferBeginInfo){
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        });

        if (rtrTimingSupported) {
            const uint32_t query = i * 2u;
            vkCmdResetQueryPool(rtrCommandBuffers[i], rtrTimingQueryPool, query, 2u);
        }

        vkCmdPipelineBarrier(rtrCommandBuffers[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &(VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = rtrSwapImages[i],
            .subresourceRange = imageRange,
        });

        if (rtrTimingSupported) {
            vkCmdWriteTimestamp(rtrCommandBuffers[i],
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                rtrTimingQueryPool,
                                i * 2u);
        }

        vkCmdPipelineBarrier(rtrCommandBuffers[i],
                             VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u,
                             0u, NULL, 1u, &(VkBufferMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = rtrMemoryBuffer,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        }, 0u, NULL);

        vkCmdBindPipeline(rtrCommandBuffers[i], VK_PIPELINE_BIND_POINT_COMPUTE, rtrPipeline);
        vkCmdBindDescriptorSets(rtrCommandBuffers[i], VK_PIPELINE_BIND_POINT_COMPUTE,
                                rtrPipelineLayout, 0u, 1u, &rtrDescriptorSets[i], 0u, NULL);
        vkCmdDispatch(rtrCommandBuffers[i],
                      (rtrSwapExtent.width + RTR_TILE_SIZE - 1u) / RTR_TILE_SIZE,
                      (rtrSwapExtent.height + RTR_TILE_SIZE - 1u) / RTR_TILE_SIZE,
                      1u);

        if (rtrTimingSupported) {
            vkCmdWriteTimestamp(rtrCommandBuffers[i],
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                rtrTimingQueryPool,
                                i * 2u + 1u);
        }

        vkCmdPipelineBarrier(rtrCommandBuffers[i], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &(VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = rtrSwapImages[i],
            .subresourceRange = imageRange,
        });

        vkEndCommandBuffer(rtrCommandBuffers[i]);
    }

    vkCreateSemaphore(rtrDevice, &(VkSemaphoreCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    }, NULL, &rtrImageAvailableSemaphore);

    vkCreateSemaphore(rtrDevice, &(VkSemaphoreCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    }, NULL, &rtrRenderFinishedSemaphore);

    vkCreateFence(rtrDevice, &(VkFenceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    }, NULL, &rtrInFlightFence);

    return 0;
}

void rtrVulkanFrame(void)
{
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    vkWaitForFences(rtrDevice, 1u, &rtrInFlightFence, VK_TRUE, UINT64_MAX);
    rtrReportGpuTiming();
    rtrUpdateMemory();
    vkResetFences(rtrDevice, 1u, &rtrInFlightFence);

    uint32_t imageIndex = 0u;
    vkAcquireNextImageKHR(rtrDevice, rtrSwapchain, UINT64_MAX,
                          rtrImageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    vkQueueSubmit(rtrQueue, 1u, &(VkSubmitInfo){
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1u,
        .pWaitSemaphores = &rtrImageAvailableSemaphore,
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1u,
        .pCommandBuffers = &rtrCommandBuffers[imageIndex],
        .signalSemaphoreCount = 1u,
        .pSignalSemaphores = &rtrRenderFinishedSemaphore,
    }, rtrInFlightFence);

    vkQueuePresentKHR(rtrQueue, &(VkPresentInfoKHR){
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1u,
        .pWaitSemaphores = &rtrRenderFinishedSemaphore,
        .swapchainCount = 1u,
        .pSwapchains = &rtrSwapchain,
        .pImageIndices = &imageIndex,
    });

    if (rtrTimingSupported) {
        rtrTimingPending = 1u;
        rtrTimingImageIndex = imageIndex;
        rtrTimingFrameIndex = rtrFrameIndex;
    }

    rtrFrameIndex++;
}
