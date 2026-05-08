#define VK_USE_PLATFORM_METAL_EXT
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "trace_comp_spv.h"
#include "platform.h"

#define SWAP_IMAGE_COUNT  3
#define COMPUTE_TILE_SIZE 8
#define LEN(a)            ((uint32_t)(sizeof(a) / sizeof(*(a))))

static const VkFormat SWAP_FORMAT = VK_FORMAT_B8G8R8A8_UNORM;
static const VkFormat RENDER_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

typedef struct Push {
    float    time;
    uint32_t frameIndex;
} Push;

static const char *const INSTANCE_EXTS[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_EXT_METAL_SURFACE_EXTENSION_NAME,
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
};
static const char *const DEVICE_EXTS[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME,
};

static VkInstance            instance;
static VkSurfaceKHR          surface;
static VkPhysicalDevice      physicalDevice;
static VkDevice              device;
static VkQueue               queue;
static VkSwapchainKHR        swapchain;
static VkExtent2D            swapExtent;
static VkImage               swapImages[SWAP_IMAGE_COUNT];
static VkImage               renderImage;
static VkDeviceMemory        renderMemory;
static VkImageView           renderImageView;
static VkDescriptorSetLayout descriptorSetLayout;
static VkDescriptorPool      descriptorPool;
static VkDescriptorSet       descriptorSet;
static VkPipelineLayout      pipelineLayout;
static VkPipeline            pipeline;
static VkCommandPool         commandPool;
static VkCommandBuffer       cb;
static VkSemaphore           imageAvailable;
static VkSemaphore           renderFinished;
static VkFence               inFlight;

static double nowSeconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &props);

    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if ((bits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    }

    abort();
}

static void imageBarrier(
    VkCommandBuffer cb,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkAccessFlags srcAccess,
    VkAccessFlags dstAccess,
    VkPipelineStageFlags srcStage,
    VkPipelineStageFlags dstStage)
{
    vkCmdPipelineBarrier(cb,
        srcStage, dstStage,
        0, 0, NULL, 0, NULL, 1, &(VkImageMemoryBarrier){
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = srcAccess,
            .dstAccessMask       = dstAccess,
            .oldLayout           = oldLayout,
            .newLayout           = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = image,
            .subresourceRange    = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        });
}

static void recordCommandBuffer(
    VkCommandBuffer cb,
    VkDescriptorSet ds,
    VkImage renderImage,
    VkImage swapImage,
    VkImageLayout renderOldLayout,
    Push push)
{
    VkImageBlit blit = {
        .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        },
        .srcOffsets = {
            { 0, 0, 0 },
            { (int32_t)swapExtent.width, (int32_t)swapExtent.height, 1 },
        },
        .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        },
        .dstOffsets = {
            { 0, 0, 0 },
            { (int32_t)swapExtent.width, (int32_t)swapExtent.height, 1 },
        },
    };

    vkBeginCommandBuffer(cb, &(VkCommandBufferBeginInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    });

    imageBarrier(cb,
        renderImage,
        renderOldLayout, VK_IMAGE_LAYOUT_GENERAL,
        0, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    imageBarrier(cb,
        swapImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &ds, 0, NULL);
    vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(cb,
        (swapExtent.width  + COMPUTE_TILE_SIZE - 1) / COMPUTE_TILE_SIZE,
        (swapExtent.height + COMPUTE_TILE_SIZE - 1) / COMPUTE_TILE_SIZE,
        1);

    imageBarrier(cb,
        renderImage,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    vkCmdBlitImage(cb,
        renderImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blit,
        VK_FILTER_NEAREST);

    imageBarrier(cb,
        renderImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_TRANSFER_READ_BIT, 0,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    imageBarrier(cb,
        swapImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_TRANSFER_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    vkEndCommandBuffer(cb);
}

static void createRenderImage(void)
{
    VkFormatProperties renderProps;
    VkFormatProperties swapProps;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, RENDER_FORMAT, &renderProps);
    vkGetPhysicalDeviceFormatProperties(physicalDevice, SWAP_FORMAT, &swapProps);

    if ((renderProps.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0 ||
        (renderProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) == 0 ||
        (swapProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) == 0)
        abort();

    vkCreateImage(device, &(VkImageCreateInfo){
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = RENDER_FORMAT,
        .extent        = {
            .width  = swapExtent.width,
            .height = swapExtent.height,
            .depth  = 1,
        },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    }, NULL, &renderImage);

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, renderImage, &req);
    vkAllocateMemory(device, &(VkMemoryAllocateInfo){
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = req.size,
        .memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    }, NULL, &renderMemory);
    vkBindImageMemory(device, renderImage, renderMemory, 0);

    vkCreateImageView(device, &(VkImageViewCreateInfo){
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = renderImage,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = RENDER_FORMAT,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    }, NULL, &renderImageView);
}

static void updateRenderDescriptor(void)
{
    vkUpdateDescriptorSets(device, 1, &(VkWriteDescriptorSet){
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = descriptorSet,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo      = &(VkDescriptorImageInfo){
            .imageView   = renderImageView,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        },
    }, 0, NULL);
}

int main(void)
{
    rtrInitWindow(1280, 720, "realtimerays");
    vkCreateInstance(&(VkInstanceCreateInfo){
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags                   = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
        .pApplicationInfo        = &(VkApplicationInfo){
            .sType      = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .apiVersion = VK_API_VERSION_1_3,
        },
        .enabledExtensionCount   = LEN(INSTANCE_EXTS),
        .ppEnabledExtensionNames = INSTANCE_EXTS,
    }, NULL, &instance);

    vkCreateMetalSurfaceEXT(instance, &(VkMetalSurfaceCreateInfoEXT){
        .sType  = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
        .pLayer = surface_layer,
    }, NULL, &surface);

    vkEnumeratePhysicalDevices(instance, &(uint32_t){1}, &physicalDevice);

    vkCreateDevice(physicalDevice, &(VkDeviceCreateInfo){
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &(VkDeviceQueueCreateInfo){
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueCount       = 1,
            .pQueuePriorities = &(float){1.0f},
        },
        .enabledExtensionCount   = LEN(DEVICE_EXTS),
        .ppEnabledExtensionNames = DEVICE_EXTS,
    }, NULL, &device);

    vkGetDeviceQueue(device, 0, 0, &queue);

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);
    if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
        abort();

    swapExtent = caps.currentExtent;
    vkCreateSwapchainKHR(device, &(VkSwapchainCreateInfoKHR){
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = surface,
        .minImageCount    = SWAP_IMAGE_COUNT,
        .imageFormat      = SWAP_FORMAT,
        .imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent      = swapExtent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = caps.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = VK_PRESENT_MODE_FIFO_KHR,
        .clipped          = VK_TRUE,
    }, NULL, &swapchain);

    vkGetSwapchainImagesKHR(device, swapchain, &(uint32_t){SWAP_IMAGE_COUNT}, swapImages);
    createRenderImage();

    vkCreateDescriptorSetLayout(device, &(VkDescriptorSetLayoutCreateInfo){
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &(VkDescriptorSetLayoutBinding){
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    }, NULL, &descriptorSetLayout);

    vkCreateDescriptorPool(device, &(VkDescriptorPoolCreateInfo){
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = 1,
        .poolSizeCount = 1,
        .pPoolSizes    = &(VkDescriptorPoolSize){
            .type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
        },
    }, NULL, &descriptorPool);

    vkAllocateDescriptorSets(device, &(VkDescriptorSetAllocateInfo){
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &descriptorSetLayout,
    }, &descriptorSet);
    updateRenderDescriptor();

    vkCreatePipelineLayout(device, &(VkPipelineLayoutCreateInfo){
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &descriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &(VkPushConstantRange){
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset     = 0,
            .size       = sizeof(Push),
        },
    }, NULL, &pipelineLayout);

    VkShaderModule shaderModule;
    vkCreateShaderModule(device, &(VkShaderModuleCreateInfo){
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = traceCompSpv_size,
        .pCode    = traceCompSpv,
    }, NULL, &shaderModule);

    vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &(VkComputePipelineCreateInfo){
        .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage  = {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shaderModule,
            .pName  = "main",
        },
        .layout = pipelineLayout,
    }, NULL, &pipeline);

    vkDestroyShaderModule(device, shaderModule, NULL);

    vkCreateCommandPool(device, &(VkCommandPoolCreateInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    }, NULL, &commandPool);

    vkAllocateCommandBuffers(device, &(VkCommandBufferAllocateInfo){
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = commandPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    }, &cb);

    vkCreateSemaphore(device, &(VkSemaphoreCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    }, NULL, &imageAvailable);
    vkCreateSemaphore(device, &(VkSemaphoreCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    }, NULL, &renderFinished);
    vkCreateFence(device, &(VkFenceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    }, NULL, &inFlight);

    double startTime = nowSeconds();
    uint32_t frameIndex = 0;

    while (rtrPumpEventsOnce() == 0) {
        vkWaitForFences(device, 1, &inFlight, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &inFlight);

        uint32_t imageIndex;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailable, VK_NULL_HANDLE, &imageIndex);

        float time = (float)(nowSeconds() - startTime);
        Push push = {
            .time = time,
            .frameIndex = frameIndex,
        };
        VkImageLayout renderOldLayout = frameIndex == 0 ?
            VK_IMAGE_LAYOUT_UNDEFINED :
            VK_IMAGE_LAYOUT_GENERAL;

        vkResetCommandBuffer(cb, 0);
        recordCommandBuffer(cb, descriptorSet, renderImage, swapImages[imageIndex], renderOldLayout, push);
        frameIndex++;

        vkQueueSubmit(queue, 1, &(VkSubmitInfo){
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount   = 1,
            .pWaitSemaphores      = &imageAvailable,
            .pWaitDstStageMask    = &(VkPipelineStageFlags){ VK_PIPELINE_STAGE_TRANSFER_BIT },
            .commandBufferCount   = 1,
            .pCommandBuffers      = &cb,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores    = &renderFinished,
        }, inFlight);

        vkQueuePresentKHR(queue, &(VkPresentInfoKHR){
            .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores    = &renderFinished,
            .swapchainCount     = 1,
            .pSwapchains        = &swapchain,
            .pImageIndices      = &imageIndex,
        });
    }

    return 0;
}
