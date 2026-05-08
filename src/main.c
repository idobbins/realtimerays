#define VK_USE_PLATFORM_METAL_EXT
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include <stdint.h>
#include "gradient_comp_spv.h"
#include "platform.h"

#define FRAME_COUNT       3
#define COMPUTE_TILE_SIZE 8
#define LEN(a)            ((uint32_t)(sizeof(a) / sizeof(*(a))))

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
static VkImage               swapImages[FRAME_COUNT];
static VkImageView           swapImageViews[FRAME_COUNT];
static VkDescriptorSetLayout descriptorSetLayout;
static VkDescriptorPool      descriptorPool;
static VkDescriptorSet       descriptorSet;
static VkPipelineLayout      pipelineLayout;
static VkPipeline            pipeline;
static VkCommandPool         commandPool;
static VkCommandBuffer       commandBuffers[FRAME_COUNT];
static VkSemaphore           imageAvailable[FRAME_COUNT];
static VkSemaphore           renderFinished[FRAME_COUNT];
static VkFence               inFlight[FRAME_COUNT];
static void recordCommandBuffer(VkCommandBuffer cb, VkDescriptorSet ds, VkImage image)
{
    VkImageSubresourceRange range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1,
        .layerCount = 1,
    };

    vkBeginCommandBuffer(cb, &(VkCommandBufferBeginInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    });

    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &(VkImageMemoryBarrier){
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = image,
            .subresourceRange    = range,
        });

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &ds, 0, NULL);
    vkCmdDispatch(cb,
        (swapExtent.width  + COMPUTE_TILE_SIZE - 1) / COMPUTE_TILE_SIZE,
        (swapExtent.height + COMPUTE_TILE_SIZE - 1) / COMPUTE_TILE_SIZE,
        1);

    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, NULL, 0, NULL, 1, &(VkImageMemoryBarrier){
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = image,
            .subresourceRange    = range,
        });

    vkEndCommandBuffer(cb);
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
    swapExtent = caps.currentExtent;
    vkCreateSwapchainKHR(device, &(VkSwapchainCreateInfoKHR){
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = surface,
        .minImageCount    = FRAME_COUNT,
        .imageFormat      = VK_FORMAT_B8G8R8A8_UNORM,
        .imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent      = swapExtent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_STORAGE_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = caps.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = VK_PRESENT_MODE_FIFO_KHR,
        .clipped          = VK_TRUE,
    }, NULL, &swapchain);

    vkGetSwapchainImagesKHR(device, swapchain, &(uint32_t){FRAME_COUNT}, swapImages);

    for (uint32_t i = 0; i < FRAME_COUNT; i++)
        vkCreateImageView(device, &(VkImageViewCreateInfo){
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image            = swapImages[i],
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = VK_FORMAT_B8G8R8A8_UNORM,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        }, NULL, &swapImageViews[i]);

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

    vkCreatePipelineLayout(device, &(VkPipelineLayoutCreateInfo){
        .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts    = &descriptorSetLayout,
    }, NULL, &pipelineLayout);

    VkShaderModule shaderModule;
    vkCreateShaderModule(device, &(VkShaderModuleCreateInfo){
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = gradientCompSpv_size,
        .pCode    = gradientCompSpv,
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
        .commandBufferCount = FRAME_COUNT,
    }, commandBuffers);

    for (uint32_t i = 0; i < FRAME_COUNT; i++) {
        vkCreateSemaphore(device, &(VkSemaphoreCreateInfo){
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        }, NULL, &imageAvailable[i]);
        vkCreateSemaphore(device, &(VkSemaphoreCreateInfo){
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        }, NULL, &renderFinished[i]);
        vkCreateFence(device, &(VkFenceCreateInfo){
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        }, NULL, &inFlight[i]);
    }

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    for (uint32_t f = 0; rtrPumpEventsOnce() == 0; f = (f + 1) % FRAME_COUNT) {
        vkWaitForFences(device, 1, &inFlight[f], VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &inFlight[f]);

        uint32_t imageIndex;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailable[f], VK_NULL_HANDLE, &imageIndex);

        vkUpdateDescriptorSets(device, 1, &(VkWriteDescriptorSet){
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = descriptorSet,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo      = &(VkDescriptorImageInfo){
                .imageView   = swapImageViews[imageIndex],
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            },
        }, 0, NULL);

        vkResetCommandBuffer(commandBuffers[f], 0);
        recordCommandBuffer(commandBuffers[f], descriptorSet, swapImages[imageIndex]);

        vkQueueSubmit(queue, 1, &(VkSubmitInfo){
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount   = 1,
            .pWaitSemaphores      = &imageAvailable[f],
            .pWaitDstStageMask    = &waitStage,
            .commandBufferCount   = 1,
            .pCommandBuffers      = &commandBuffers[f],
            .signalSemaphoreCount = 1,
            .pSignalSemaphores    = &renderFinished[f],
        }, inFlight[f]);

        vkQueuePresentKHR(queue, &(VkPresentInfoKHR){
            .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores    = &renderFinished[f],
            .swapchainCount     = 1,
            .pSwapchains        = &swapchain,
            .pImageIndices      = &imageIndex,
        });
    }

    return 0;
}
