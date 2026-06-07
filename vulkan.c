#if defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#else
#error Unsupported platform
#endif
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "trace_comp_spv.h"

#define RTR_MAX_SWAP_IMAGES 3u
#define RTR_TILE_SIZE 8u
#define RTR_MEMORY_HEADER_WORDS 24u
#define RTR_SCENE_SPHERE_COUNT 1000000u
#define RTR_SCENE_SPHERE_WORDS 1u
#define RTR_SCENE_BVH_NODE_COUNT (RTR_SCENE_SPHERE_COUNT * 2u - 1u)
#define RTR_SCENE_BVH_NODE_WORDS 5u
#define RTR_BLUE_NOISE_SIZE 64u
#define RTR_BLUE_NOISE_WORDS (RTR_BLUE_NOISE_SIZE * RTR_BLUE_NOISE_SIZE)
#define RTR_SCENE_WORDS \
    (RTR_SCENE_SPHERE_COUNT * RTR_SCENE_SPHERE_WORDS + \
     RTR_SCENE_BVH_NODE_COUNT * RTR_SCENE_BVH_NODE_WORDS + \
     RTR_BLUE_NOISE_WORDS)
#define RTR_MEMORY_MAGIC 0x30525452u
#define RTR_TIMING_WINDOW 100u

void rtrWindowMouse(float *x, float *y);
void rtrWindowCamera(uint32_t *autoOrbit, float *yaw, float *pitch, float *radius);
void rtrScene(uint32_t *words);

enum {
    RTR_MEMORY_MAGIC_WORD = 0,
    RTR_MEMORY_VERSION_WORD = 1,
    RTR_MEMORY_WIDTH_WORD = 2,
    RTR_MEMORY_HEIGHT_WORD = 3,
    RTR_MEMORY_FRAME_WORD = 4,
    RTR_MEMORY_MOUSE_X_WORD = 5,
    RTR_MEMORY_MOUSE_Y_WORD = 6,
    RTR_MEMORY_TIME_WORD = 7,
    RTR_MEMORY_SPHERE_COUNT_WORD = 8,
    RTR_MEMORY_CAMERA_AUTO_WORD = 16,
    RTR_MEMORY_CAMERA_YAW_WORD = 17,
    RTR_MEMORY_CAMERA_PITCH_WORD = 18,
    RTR_MEMORY_CAMERA_RADIUS_WORD = 19,
};

enum {
    RTR_CAMERA_AUTO_DEFAULT = 1u,
};

#define RTR_CAMERA_DEFAULT_YAW 0.35f
#define RTR_CAMERA_DEFAULT_PITCH 0.473f
#define RTR_CAMERA_DEFAULT_RADIUS 4.0f
#define RTR_CAMERA_AUTO_SPEED 0.08f
#define RTR_FRAME_TIME_CLAMP_SECONDS (1.0 / 20.0)

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

enum {
    RTR_DESCRIPTOR_IMAGE,
    RTR_DESCRIPTOR_MEMORY,
    RTR_DESCRIPTOR_COUNT,
};

static const VkDescriptorSetLayoutBinding rtrDescriptorBindings[RTR_DESCRIPTOR_COUNT] = {
    {
        .binding = RTR_DESCRIPTOR_IMAGE,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    },
    {
        .binding = RTR_DESCRIPTOR_MEMORY,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    },
};

static VkInstance rtrInstance = VK_NULL_HANDLE;
static VkSurfaceKHR rtrSurface = VK_NULL_HANDLE;
static VkPhysicalDevice rtrPhysicalDevice = VK_NULL_HANDLE;
static VkDevice rtrDevice = VK_NULL_HANDLE;
static VkQueue rtrQueue = VK_NULL_HANDLE;
static VkSwapchainKHR rtrSwapchain = VK_NULL_HANDLE;
static VkExtent2D rtrSwapExtent = {0u, 0u};
static VkImage rtrSwapImages[RTR_MAX_SWAP_IMAGES];
static VkImage rtrRenderImage = VK_NULL_HANDLE;
static VkImageView rtrRenderImageView = VK_NULL_HANDLE;
static VkDeviceMemory rtrRenderImageMemory = VK_NULL_HANDLE;
static VkDescriptorSetLayout rtrDescriptorSetLayout = VK_NULL_HANDLE;
static VkDescriptorPool rtrDescriptorPool = VK_NULL_HANDLE;
static VkDescriptorSet rtrDescriptorSets[RTR_MAX_SWAP_IMAGES];
static VkPipelineLayout rtrPipelineLayout = VK_NULL_HANDLE;
static VkPipeline rtrPipeline = VK_NULL_HANDLE;
static VkCommandPool rtrCommandPool = VK_NULL_HANDLE;
static VkCommandBuffer rtrCommandBuffers[RTR_MAX_SWAP_IMAGES];
static VkSemaphore rtrImageAvailableSemaphore = VK_NULL_HANDLE;
static VkSemaphore rtrRenderFinishedSemaphores[RTR_MAX_SWAP_IMAGES];
static VkFence rtrInFlightFence = VK_NULL_HANDLE;
static VkQueryPool rtrTimingQueryPool = VK_NULL_HANDLE;
static VkBuffer rtrMemoryBuffer = VK_NULL_HANDLE;
static VkDeviceMemory rtrMemoryBufferMemory = VK_NULL_HANDLE;
static VkBuffer rtrExportStagingBuffer = VK_NULL_HANDLE;
static VkDeviceMemory rtrExportStagingMemory = VK_NULL_HANDLE;
static VkDeviceSize rtrExportImageBytes = 0u;
static uint32_t *rtrMemoryWords = NULL;
static void *rtrExportStagingPixels = NULL;
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
static FILE *rtrTimingOut = NULL;
static struct timespec rtrStartTime;
static double rtrFrameClockSeconds = 0.0;
static double rtrFrameClockLastSeconds = 0.0;
static uint32_t rtrFrameClockReady = 0u;

static uint32_t rtrF32Word(float value)
{
    uint32_t word = 0u;
    memcpy(&word, &value, sizeof(word));
    return word;
}

static float rtrEnvF32(const char *name, float fallback)
{
    const char *value = getenv(name);
    if (!(value && *value)) return fallback;

    char *end = NULL;
    const float parsed = strtof(value, &end);
    return end != value ? parsed : fallback;
}

static uint32_t rtrEnvU32(const char *name, uint32_t fallback)
{
    const char *value = getenv(name);
    if (!(value && *value)) return fallback;

    char *end = NULL;
    const unsigned long parsed = strtoul(value, &end, 10);
    return end != value ? (uint32_t)parsed : fallback;
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

static uint32_t rtrFormatSupports(VkFormat format, VkFormatFeatureFlags features)
{
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(rtrPhysicalDevice, format, &props);

    return (uint32_t)((props.optimalTilingFeatures & features) == features);
}

static int rtrChooseSurfaceFormat(VkSurfaceFormatKHR *chosen)
{
    uint32_t formatCount = 0u;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(rtrPhysicalDevice, rtrSurface,
                                             &formatCount, NULL) != VK_SUCCESS ||
        formatCount == 0u) {
        fprintf(stderr, "swapchain: no surface formats available\n");
        return 1;
    }

    VkSurfaceFormatKHR *formats =
        (VkSurfaceFormatKHR *)malloc(sizeof(*formats) * (size_t)formatCount);
    if (!formats) return 1;

    if (vkGetPhysicalDeviceSurfaceFormatsKHR(rtrPhysicalDevice, rtrSurface,
                                             &formatCount, formats) != VK_SUCCESS) {
        free(formats);
        return 1;
    }

    const VkFormat preferred[] = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
    };

    for (uint32_t p = 0u; p < (uint32_t)(sizeof(preferred) / sizeof(*preferred)); p++) {
        for (uint32_t i = 0u; i < formatCount; i++) {
            if (formats[i].format == preferred[p] &&
                formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                *chosen = formats[i];
                free(formats);
                return 0;
            }
        }
    }

    for (uint32_t i = 0u; i < formatCount; i++) {
        if ((formats[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
             formats[i].format == VK_FORMAT_R8G8B8A8_UNORM)) {
            *chosen = formats[i];
            free(formats);
            return 0;
        }
    }

    fprintf(stderr, "swapchain: no rgba8/bgra8 surface format\n");
    free(formats);
    return 1;
}

static int rtrCreateRenderImage(void)
{
    if (!rtrFormatSupports(VK_FORMAT_R8G8B8A8_UNORM,
                           VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                           VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
        fprintf(stderr, "render image: R8G8B8A8_UNORM storage/blit source unsupported\n");
        return 1;
    }

    if (vkCreateImage(rtrDevice, &(VkImageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {rtrSwapExtent.width, rtrSwapExtent.height, 1u},
        .mipLevels = 1u,
        .arrayLayers = 1u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    }, NULL, &rtrRenderImage) != VK_SUCCESS) return 1;

    VkMemoryRequirements imageReq;
    vkGetImageMemoryRequirements(rtrDevice, rtrRenderImage, &imageReq);
    const uint32_t imageMemoryType = rtrFindMemoryType(
        imageReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (imageMemoryType == UINT32_MAX) return 1;

    if (vkAllocateMemory(rtrDevice, &(VkMemoryAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = imageReq.size,
        .memoryTypeIndex = imageMemoryType,
    }, NULL, &rtrRenderImageMemory) != VK_SUCCESS) return 1;
    vkBindImageMemory(rtrDevice, rtrRenderImage, rtrRenderImageMemory, 0u);

    if (vkCreateImageView(rtrDevice, &(VkImageViewCreateInfo){
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = rtrRenderImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1u,
            .layerCount = 1u,
        },
    }, NULL, &rtrRenderImageView) != VK_SUCCESS) return 1;

    return 0;
}

static double rtrElapsedSeconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    const double seconds = (double)(now.tv_sec - rtrStartTime.tv_sec);
    const double nanos = (double)(now.tv_nsec - rtrStartTime.tv_nsec) / 1000000000.0;
    return seconds + nanos;
}

static float rtrFrameSeconds(void)
{
    const double now = rtrElapsedSeconds();
    if (!rtrFrameClockReady) {
        rtrFrameClockLastSeconds = now;
        rtrFrameClockReady = 1u;
        return 0.0f;
    }

    double dt = now - rtrFrameClockLastSeconds;
    rtrFrameClockLastSeconds = now;

    if (dt < 0.0)
        dt = 0.0;
    if (dt > RTR_FRAME_TIME_CLAMP_SECONDS)
        dt = RTR_FRAME_TIME_CLAMP_SECONDS;

    rtrFrameClockSeconds += dt;
    return (float)rtrFrameClockSeconds;
}

static int rtrCreateMemoryBuffer(void)
{
    const VkDeviceSize rtrMemorySize =
        ((VkDeviceSize)RTR_MEMORY_HEADER_WORDS + (VkDeviceSize)RTR_SCENE_WORDS) *
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
    rtrMemoryWords[RTR_MEMORY_VERSION_WORD] = 6u;
    rtrMemoryWords[RTR_MEMORY_WIDTH_WORD] = rtrSwapExtent.width;
    rtrMemoryWords[RTR_MEMORY_HEIGHT_WORD] = rtrSwapExtent.height;
    rtrMemoryWords[RTR_MEMORY_MOUSE_X_WORD] = rtrF32Word(-1.0f);
    rtrMemoryWords[RTR_MEMORY_MOUSE_Y_WORD] = rtrF32Word(-1.0f);
    rtrMemoryWords[RTR_MEMORY_CAMERA_AUTO_WORD] = RTR_CAMERA_AUTO_DEFAULT;
    rtrMemoryWords[RTR_MEMORY_CAMERA_YAW_WORD] = rtrF32Word(RTR_CAMERA_DEFAULT_YAW);
    rtrMemoryWords[RTR_MEMORY_CAMERA_PITCH_WORD] = rtrF32Word(RTR_CAMERA_DEFAULT_PITCH);
    rtrMemoryWords[RTR_MEMORY_CAMERA_RADIUS_WORD] = rtrF32Word(RTR_CAMERA_DEFAULT_RADIUS);
    rtrScene(rtrMemoryWords);

    return 0;
}

static void rtrUpdateMemoryWith(float time,
                                float mouseX,
                                float mouseY,
                                uint32_t autoOrbit,
                                float cameraYaw,
                                float cameraPitch,
                                float cameraRadius)
{
    rtrMemoryWords[RTR_MEMORY_WIDTH_WORD] = rtrSwapExtent.width;
    rtrMemoryWords[RTR_MEMORY_HEIGHT_WORD] = rtrSwapExtent.height;
    rtrMemoryWords[RTR_MEMORY_FRAME_WORD] = rtrFrameIndex;
    rtrMemoryWords[RTR_MEMORY_MOUSE_X_WORD] = rtrF32Word(mouseX);
    rtrMemoryWords[RTR_MEMORY_MOUSE_Y_WORD] = rtrF32Word(mouseY);
    rtrMemoryWords[RTR_MEMORY_TIME_WORD] = rtrF32Word(time);
    const float activeCameraYaw =
        autoOrbit ? cameraYaw + time * RTR_CAMERA_AUTO_SPEED : cameraYaw;

    rtrMemoryWords[RTR_MEMORY_CAMERA_AUTO_WORD] = autoOrbit;
    rtrMemoryWords[RTR_MEMORY_CAMERA_YAW_WORD] = rtrF32Word(activeCameraYaw);
    rtrMemoryWords[RTR_MEMORY_CAMERA_PITCH_WORD] = rtrF32Word(cameraPitch);
    rtrMemoryWords[RTR_MEMORY_CAMERA_RADIUS_WORD] = rtrF32Word(cameraRadius);
}

static void rtrUpdateMemory(void)
{
    float mouseX = -1.0f;
    float mouseY = -1.0f;
    uint32_t autoOrbit = RTR_CAMERA_AUTO_DEFAULT;
    float cameraYaw = RTR_CAMERA_DEFAULT_YAW;
    float cameraPitch = RTR_CAMERA_DEFAULT_PITCH;
    float cameraRadius = RTR_CAMERA_DEFAULT_RADIUS;

    rtrWindowMouse(&mouseX, &mouseY);
    rtrWindowCamera(&autoOrbit, &cameraYaw, &cameraPitch, &cameraRadius);
    rtrUpdateMemoryWith(rtrFrameSeconds(), mouseX, mouseY,
                        autoOrbit, cameraYaw, cameraPitch, cameraRadius);
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

static uint32_t rtrTimingPercentileIndex(uint32_t count, uint32_t percentile)
{
    uint32_t rank = (count * percentile) / 100u;
    if (rank == 0u) rank = 1u;
    if (rank > count) rank = count;
    return rank - 1u;
}

static void rtrReportGpuTimingWindow(uint32_t count)
{
    if (count == 0u) return;

    double sorted[RTR_TIMING_WINDOW];
    double sum = 0.0;
    for (uint32_t i = 0u; i < count; i++) {
        sorted[i] = rtrTimingSamples[i];
        sum += rtrTimingSamples[i];
    }

    rtrSortTimingSamples(sorted, count);

    const double avg = sum / (double)count;
    const double median = (count & 1u) ?
        sorted[count / 2u] :
        (sorted[count / 2u - 1u] + sorted[count / 2u]) * 0.5;
    const double p01 = sorted[rtrTimingPercentileIndex(count, 1u)];
    const double p95 = sorted[rtrTimingPercentileIndex(count, 95u)];
    const double p99 = sorted[rtrTimingPercentileIndex(count, 99u)];

    double variance = 0.0;
    for (uint32_t i = 0u; i < count; i++) {
        const double delta = rtrTimingSamples[i] - avg;
        variance += delta * delta;
    }
    variance /= (double)count;
    const double stddev = sqrt(variance);
    const double cvPercent = avg > 0.0 ? (stddev / avg) * 100.0 : 0.0;

    double jitterStddev = 0.0;
    if (count > 1u) {
        double jitterSum = 0.0;
        for (uint32_t i = 1u; i < count; i++) {
            jitterSum += rtrTimingSamples[i] - rtrTimingSamples[i - 1u];
        }

        const uint32_t jitterCount = count - 1u;
        const double jitterAvg = jitterSum / (double)jitterCount;
        double jitterVariance = 0.0;
        for (uint32_t i = 1u; i < count; i++) {
            const double delta =
                (rtrTimingSamples[i] - rtrTimingSamples[i - 1u]) - jitterAvg;
            jitterVariance += delta * delta;
        }
        jitterVariance /= (double)jitterCount;
        jitterStddev = sqrt(jitterVariance);
    }

    FILE *out = rtrTimingOut ? rtrTimingOut : stdout;
    fprintf(out,
            "gpu[%u] frames %u-%u %ux%u avg %.3f ms med %.3f ms min %.3f p01 %.3f p95 %.3f p99 %.3f max %.3f stddev %.3f ms cv %.1f%% jitter %.3f ms\n",
            count,
            rtrTimingWindowFirstFrame,
            rtrTimingFrameIndex,
            rtrSwapExtent.width,
            rtrSwapExtent.height,
            avg,
            median,
            sorted[0],
            p01,
            p95,
            p99,
            sorted[count - 1u],
            stddev,
            cvPercent,
            jitterStddev);
    fflush(out);
}

static void rtrPushGpuTiming(double gpuMs)
{
    if (rtrTimingSampleCount == 0u) {
        rtrTimingWindowFirstFrame = rtrTimingFrameIndex;
    }

    rtrTimingSamples[rtrTimingSampleCount++] = gpuMs;
    if (rtrTimingSampleCount < RTR_TIMING_WINDOW) return;

    rtrReportGpuTimingWindow(rtrTimingSampleCount);
    rtrTimingSampleCount = 0u;
}

static void rtrFlushGpuTiming(void)
{
    if (rtrTimingSampleCount == 0u) return;

    rtrReportGpuTimingWindow(rtrTimingSampleCount);
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
    const uint32_t windowed = (uint32_t)(windowSurface != NULL);
    rtrTimingOut = windowed ? stdout : stderr;
    clock_gettime(CLOCK_MONOTONIC, &rtrStartTime);
    rtrFrameIndex = 0u;
    rtrFrameClockSeconds = 0.0;
    rtrFrameClockLastSeconds = 0.0;
    rtrFrameClockReady = 0u;

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

    if (windowed) {
        vkCreateMetalSurfaceEXT(rtrInstance, &(VkMetalSurfaceCreateInfoEXT){
            .sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
            .pLayer = windowSurface,
        }, NULL, &rtrSurface);
    } else if (rtrSwapExtent.width == 0u || rtrSwapExtent.height == 0u) {
        return 1;
    }

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

    uint32_t imageCount = 1u;
    if (windowed) {
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(rtrPhysicalDevice, rtrSurface, &caps);
        rtrSwapExtent = caps.currentExtent;
        if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0u) {
            fprintf(stderr, "swapchain: transfer destination usage unsupported\n");
            return 1;
        }

        VkSurfaceFormatKHR surfaceFormat;
        if (rtrChooseSurfaceFormat(&surfaceFormat)) return 1;
        if (!rtrFormatSupports(surfaceFormat.format, VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
            fprintf(stderr, "swapchain: selected format cannot receive image blits\n");
            return 1;
        }
        uint32_t swapchainMinImageCount = RTR_MAX_SWAP_IMAGES;
        if (swapchainMinImageCount < caps.minImageCount) swapchainMinImageCount = caps.minImageCount;
        if ((caps.maxImageCount != 0u) && (swapchainMinImageCount > caps.maxImageCount)) {
            swapchainMinImageCount = caps.maxImageCount;
        }

        vkCreateSwapchainKHR(rtrDevice, &(VkSwapchainCreateInfoKHR){
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = rtrSurface,
            .minImageCount = swapchainMinImageCount,
            .imageFormat = surfaceFormat.format,
            .imageColorSpace = surfaceFormat.colorSpace,
            .imageExtent = rtrSwapExtent,
            .imageArrayLayers = 1u,
            .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .preTransform = caps.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = VK_PRESENT_MODE_FIFO_KHR,
            .clipped = VK_TRUE,
        }, NULL, &rtrSwapchain);

        vkGetSwapchainImagesKHR(rtrDevice, rtrSwapchain, &imageCount, NULL);
        if ((imageCount < 2u) || (imageCount > RTR_MAX_SWAP_IMAGES)) return 1;
        vkGetSwapchainImagesKHR(rtrDevice, rtrSwapchain, &imageCount, rtrSwapImages);
    } else {
        rtrExportImageBytes =
            (VkDeviceSize)rtrSwapExtent.width *
            (VkDeviceSize)rtrSwapExtent.height *
            (VkDeviceSize)4u;

        vkCreateBuffer(rtrDevice, &(VkBufferCreateInfo){
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = rtrExportImageBytes,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        }, NULL, &rtrExportStagingBuffer);

        VkMemoryRequirements stagingReq;
        vkGetBufferMemoryRequirements(rtrDevice, rtrExportStagingBuffer, &stagingReq);
        uint32_t stagingMemoryType = rtrFindMemoryType(
            stagingReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(rtrDevice, &(VkMemoryAllocateInfo){
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = stagingReq.size,
            .memoryTypeIndex = stagingMemoryType,
        }, NULL, &rtrExportStagingMemory);
        vkBindBufferMemory(rtrDevice, rtrExportStagingBuffer, rtrExportStagingMemory, 0u);
        vkMapMemory(rtrDevice, rtrExportStagingMemory, 0u, rtrExportImageBytes, 0u,
                    &rtrExportStagingPixels);
    }

    if (rtrCreateRenderImage()) return 1;
    if (rtrCreateMemoryBuffer()) return 1;
    if (rtrCreateTimingQueryPool()) return 1;

    vkCreateDescriptorSetLayout(rtrDevice, &(VkDescriptorSetLayoutCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = RTR_DESCRIPTOR_COUNT,
        .pBindings = rtrDescriptorBindings,
    }, NULL, &rtrDescriptorSetLayout);

    VkDescriptorPoolSize poolSizes[RTR_DESCRIPTOR_COUNT];
    for (uint32_t binding = 0u; binding < RTR_DESCRIPTOR_COUNT; binding++) {
        poolSizes[binding] = (VkDescriptorPoolSize){
            .type = rtrDescriptorBindings[binding].descriptorType,
            .descriptorCount = imageCount,
        };
    }

    vkCreateDescriptorPool(rtrDevice, &(VkDescriptorPoolCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = imageCount,
        .poolSizeCount = RTR_DESCRIPTOR_COUNT,
        .pPoolSizes = poolSizes,
    }, NULL, &rtrDescriptorPool);

    VkDescriptorSetLayout setLayouts[RTR_MAX_SWAP_IMAGES];
    for (uint32_t i = 0u; i < imageCount; i++) {
        setLayouts[i] = rtrDescriptorSetLayout;
    }

    vkAllocateDescriptorSets(rtrDevice, &(VkDescriptorSetAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = rtrDescriptorPool,
        .descriptorSetCount = imageCount,
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
        .flags = windowed ? 0u : VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = 0u,
    }, NULL, &rtrCommandPool);

    vkAllocateCommandBuffers(rtrDevice, &(VkCommandBufferAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = rtrCommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = imageCount,
    }, rtrCommandBuffers);

    VkImageSubresourceRange imageRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1u,
        .layerCount = 1u,
    };

    for (uint32_t i = 0u; i < imageCount; i++) {
        const VkDescriptorImageInfo imageInfo = {
            .imageView = rtrRenderImageView,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
        const VkDescriptorBufferInfo bufferInfo = {
            .buffer = rtrMemoryBuffer,
            .offset = 0u,
            .range = VK_WHOLE_SIZE,
        };
        VkWriteDescriptorSet writes[RTR_DESCRIPTOR_COUNT];
        for (uint32_t binding = 0u; binding < RTR_DESCRIPTOR_COUNT; binding++) {
            writes[binding] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = rtrDescriptorSets[i],
                .dstBinding = rtrDescriptorBindings[binding].binding,
                .descriptorCount = 1u,
                .descriptorType = rtrDescriptorBindings[binding].descriptorType,
            };
        }
        writes[RTR_DESCRIPTOR_IMAGE].pImageInfo = &imageInfo;
        writes[RTR_DESCRIPTOR_MEMORY].pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(rtrDevice, RTR_DESCRIPTOR_COUNT, writes, 0u, NULL);

        if (!windowed) continue;

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
            .image = rtrRenderImage,
            .subresourceRange = imageRange,
        });

        vkCmdPipelineBarrier(rtrCommandBuffers[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &(VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
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
            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
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
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &(VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = rtrRenderImage,
            .subresourceRange = imageRange,
        });

        vkCmdBlitImage(rtrCommandBuffers[i],
                       rtrRenderImage,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       rtrSwapImages[i],
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1u,
                       &(VkImageBlit){
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1u,
            },
            .srcOffsets = {
                {0, 0, 0},
                {(int32_t)rtrSwapExtent.width, (int32_t)rtrSwapExtent.height, 1},
            },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1u,
            },
            .dstOffsets = {
                {0, 0, 0},
                {(int32_t)rtrSwapExtent.width, (int32_t)rtrSwapExtent.height, 1},
            },
        }, VK_FILTER_NEAREST);

        vkCmdPipelineBarrier(rtrCommandBuffers[i], VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &(VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = rtrSwapImages[i],
            .subresourceRange = imageRange,
        });

        vkEndCommandBuffer(rtrCommandBuffers[i]);
    }

    if (windowed) {
        vkCreateSemaphore(rtrDevice, &(VkSemaphoreCreateInfo){
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        }, NULL, &rtrImageAvailableSemaphore);

        for (uint32_t i = 0u; i < imageCount; i++) {
            vkCreateSemaphore(rtrDevice, &(VkSemaphoreCreateInfo){
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            }, NULL, &rtrRenderFinishedSemaphores[i]);
        }

        vkCreateFence(rtrDevice, &(VkFenceCreateInfo){
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        }, NULL, &rtrInFlightFence);
    } else {
        vkCreateFence(rtrDevice, &(VkFenceCreateInfo){
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        }, NULL, &rtrInFlightFence);
    }

    return 0;
}

void rtrVulkanFrame(void)
{
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    vkWaitForFences(rtrDevice, 1u, &rtrInFlightFence, VK_TRUE, UINT64_MAX);
    rtrReportGpuTiming();
    rtrUpdateMemory();

    uint32_t imageIndex = 0u;
    VkResult result = vkAcquireNextImageKHR(rtrDevice, rtrSwapchain, UINT64_MAX,
                                            rtrImageAvailableSemaphore,
                                            VK_NULL_HANDLE,
                                            &imageIndex);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return;

    VkSemaphore renderFinished = rtrRenderFinishedSemaphores[imageIndex];
    vkResetFences(rtrDevice, 1u, &rtrInFlightFence);

    vkQueueSubmit(rtrQueue, 1u, &(VkSubmitInfo){
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1u,
        .pWaitSemaphores = &rtrImageAvailableSemaphore,
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1u,
        .pCommandBuffers = &rtrCommandBuffers[imageIndex],
        .signalSemaphoreCount = 1u,
        .pSignalSemaphores = &renderFinished,
    }, rtrInFlightFence);

    result = vkQueuePresentKHR(rtrQueue, &(VkPresentInfoKHR){
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1u,
        .pWaitSemaphores = &renderFinished,
        .swapchainCount = 1u,
        .pSwapchains = &rtrSwapchain,
        .pImageIndices = &imageIndex,
    });
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return;

    if (rtrTimingSupported) {
        rtrTimingPending = 1u;
        rtrTimingImageIndex = imageIndex;
        rtrTimingFrameIndex = rtrFrameIndex;
    }

    rtrFrameIndex++;
}

static void rtrRecordExportFrame(VkCommandBuffer commandBuffer,
                                 VkImage image,
                                 VkBuffer stagingBuffer,
                                 VkImageLayout oldLayout)
{
    const VkImageSubresourceRange imageRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1u,
        .layerCount = 1u,
    };
    vkBeginCommandBuffer(commandBuffer, &(VkCommandBufferBeginInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    });

    if (rtrTimingSupported) {
        vkCmdResetQueryPool(commandBuffer, rtrTimingQueryPool, 0u, 2u);
    }

    vkCmdPipelineBarrier(commandBuffer,
                         oldLayout == VK_IMAGE_LAYOUT_UNDEFINED ?
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT :
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0u, 0u, NULL, 0u, NULL, 1u, &(VkImageMemoryBarrier){
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0u : VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = oldLayout,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = imageRange,
    });

    if (rtrTimingSupported) {
        vkCmdWriteTimestamp(commandBuffer,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            rtrTimingQueryPool,
                            0u);
    }

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u,
                         0u, NULL, 1u, &(VkBufferMemoryBarrier){
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = rtrMemoryBuffer,
        .offset = 0u,
        .size = VK_WHOLE_SIZE,
    }, 0u, NULL);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, rtrPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            rtrPipelineLayout, 0u, 1u, &rtrDescriptorSets[0], 0u, NULL);
    vkCmdDispatch(commandBuffer,
                  (rtrSwapExtent.width + RTR_TILE_SIZE - 1u) / RTR_TILE_SIZE,
                  (rtrSwapExtent.height + RTR_TILE_SIZE - 1u) / RTR_TILE_SIZE,
                  1u);

    if (rtrTimingSupported) {
        vkCmdWriteTimestamp(commandBuffer,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            rtrTimingQueryPool,
                            1u);
    }

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0u, 0u, NULL, 0u, NULL, 1u, &(VkImageMemoryBarrier){
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = imageRange,
    });

    vkCmdCopyImageToBuffer(commandBuffer,
                           image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           stagingBuffer,
                           1u,
                           &(VkBufferImageCopy){
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1u,
        },
        .imageExtent = {
            .width = rtrSwapExtent.width,
            .height = rtrSwapExtent.height,
            .depth = 1u,
        },
    });

    vkEndCommandBuffer(commandBuffer);
}

int rtrVulkanWriteFrames(FILE *out, uint32_t width, uint32_t height, uint32_t frames, uint32_t fps)
{
    rtrSwapExtent = (VkExtent2D){width, height};
    if (rtrVulkanInit(NULL)) return 1;

    const uint32_t autoOrbit =
        rtrEnvU32("RTR_XPOST_AUTO_ORBIT", RTR_CAMERA_AUTO_DEFAULT) ? 1u : 0u;
    const float cameraYaw =
        rtrEnvF32("RTR_XPOST_CAMERA_YAW", RTR_CAMERA_DEFAULT_YAW);
    const float cameraPitch =
        rtrEnvF32("RTR_XPOST_CAMERA_PITCH", RTR_CAMERA_DEFAULT_PITCH);
    const float cameraRadius =
        rtrEnvF32("RTR_XPOST_CAMERA_RADIUS", RTR_CAMERA_DEFAULT_RADIUS);

    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    for (uint32_t frame = 0u; frame < frames; frame++) {
        rtrUpdateMemoryWith((float)frame / (float)fps, -1.0f, -1.0f,
                            autoOrbit,
                            cameraYaw,
                            cameraPitch,
                            cameraRadius);
        vkResetCommandBuffer(rtrCommandBuffers[0], 0u);
        rtrRecordExportFrame(rtrCommandBuffers[0], rtrRenderImage,
                             rtrExportStagingBuffer, oldLayout);
        oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        vkQueueSubmit(rtrQueue, 1u, &(VkSubmitInfo){
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1u,
            .pCommandBuffers = &rtrCommandBuffers[0],
        }, rtrInFlightFence);
        if (rtrTimingSupported) {
            rtrTimingPending = 1u;
            rtrTimingImageIndex = 0u;
            rtrTimingFrameIndex = frame;
        }
        vkWaitForFences(rtrDevice, 1u, &rtrInFlightFence, VK_TRUE, UINT64_MAX);
        rtrReportGpuTiming();
        vkResetFences(rtrDevice, 1u, &rtrInFlightFence);

        fwrite(rtrExportStagingPixels, 1u, (size_t)rtrExportImageBytes, out);
        rtrFrameIndex++;
    }

    rtrFlushGpuTiming();

    return 0;
}
