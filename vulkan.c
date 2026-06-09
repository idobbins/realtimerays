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

#include "shatter_comp_spv.h"
#include "physics_comp_spv.h"
#include "render_comp_spv.h"

#if defined(__GNUC__) || defined(__clang__)
#define RTR_UNUSED __attribute__((unused))
#else
#define RTR_UNUSED
#endif

#define RTR_MAX_SWAP_IMAGES 3u
#define RTR_TILE_SIZE 8u
#define RTR_MEMORY_HEADER_WORDS 32u
#define RTR_SCENE_BRICK_SIZE 4u
#define RTR_SCENE_BRICK_GRID_X 32u
#define RTR_SCENE_BRICK_GRID_Y 12u
#define RTR_SCENE_BRICK_GRID_Z 32u
#define RTR_SCENE_BRICK_CAPACITY \
    (RTR_SCENE_BRICK_GRID_X * RTR_SCENE_BRICK_GRID_Y * RTR_SCENE_BRICK_GRID_Z)
#define RTR_SCENE_BRICK_WORDS 2u
#define RTR_ENVMAP_WIDTH 1024u
#define RTR_ENVMAP_HEIGHT 512u
#define RTR_ENVMAP_WORDS (RTR_ENVMAP_WIDTH * RTR_ENVMAP_HEIGHT * 2u)
#define RTR_ENVMAP_DIFFUSE_WIDTH 32u
#define RTR_ENVMAP_DIFFUSE_HEIGHT 16u
#define RTR_ENVMAP_DIFFUSE_WORDS \
    (RTR_ENVMAP_DIFFUSE_WIDTH * RTR_ENVMAP_DIFFUSE_HEIGHT * 2u)
#define RTR_SCENE_WORDS \
    (RTR_SCENE_BRICK_CAPACITY + \
     RTR_SCENE_BRICK_CAPACITY * RTR_SCENE_BRICK_WORDS + \
     RTR_ENVMAP_WORDS + \
     RTR_ENVMAP_DIFFUSE_WORDS)
#define RTR_MEMORY_MAGIC 0x30525452u
#define RTR_TIMING_WINDOW 100u

void rtrWindowCamera(uint32_t *autoOrbit, float *yaw, float *pitch, float *radius);
void rtrWindowSetCameraYaw(float yaw);
int rtrWindowConsumeImpulse(float *x, float *y);
void rtrScene(uint32_t *words);

enum {
    RTR_MEMORY_MAGIC_WORD = 0,
    RTR_MEMORY_VERSION_WORD = 1,
    RTR_MEMORY_WIDTH_WORD = 2,
    RTR_MEMORY_HEIGHT_WORD = 3,
    RTR_MEMORY_FRAME_WORD = 4,
    RTR_MEMORY_BRICK_COUNT_WORD = 8,
    RTR_MEMORY_SCENE_MIN_WORD = 24,
    RTR_MEMORY_SCENE_MAX_WORD = 27,
    RTR_MEMORY_VOXEL_MAP_WORD = 32,
    RTR_MEMORY_CAMERA_YAW_WORD = 17,
    RTR_MEMORY_CAMERA_PITCH_WORD = 18,
    RTR_MEMORY_CAMERA_RADIUS_WORD = 19,
    RTR_MEMORY_CHUNK_COUNT_WORD = 20,
    RTR_MEMORY_DT_WORD = 21,
    RTR_MEMORY_IMPULSE_PENDING_WORD = 22,
    RTR_MEMORY_IMPULSE_X_WORD = 23,
    RTR_MEMORY_IMPULSE_Y_WORD = 30,
    RTR_MEMORY_IMPULSE_SEED_WORD = 31,
};

enum {
    RTR_CHUNK_ACTIVE_WORD = 0,
    RTR_CHUNK_BASE_X_WORD = 1,
    RTR_CHUNK_BASE_Y_WORD = 2,
    RTR_CHUNK_BASE_Z_WORD = 3,
    RTR_CHUNK_POSITION_X_WORD = 4,
    RTR_CHUNK_POSITION_Y_WORD = 5,
    RTR_CHUNK_POSITION_Z_WORD = 6,
    RTR_CHUNK_AWAKE_WORD = 7,
    RTR_CHUNK_ROTATION_X_WORD = 8,
    RTR_CHUNK_ROTATION_Y_WORD = 9,
    RTR_CHUNK_ROTATION_Z_WORD = 10,
    RTR_CHUNK_ROTATION_W_WORD = 11,
    RTR_CHUNK_VOXEL_COUNT_WORD = 12,
    RTR_CHUNK_VELOCITY_X_WORD = 13,
    RTR_CHUNK_VELOCITY_Y_WORD = 14,
    RTR_CHUNK_VELOCITY_Z_WORD = 15,
    RTR_CHUNK_LOCAL_MIN_X_WORD = 16,
    RTR_CHUNK_LOCAL_MIN_Y_WORD = 17,
    RTR_CHUNK_LOCAL_MIN_Z_WORD = 18,
    RTR_CHUNK_INV_MASS_WORD = 19,
    RTR_CHUNK_LOCAL_MAX_X_WORD = 20,
    RTR_CHUNK_LOCAL_MAX_Y_WORD = 21,
    RTR_CHUNK_LOCAL_MAX_Z_WORD = 22,
    RTR_CHUNK_GRID_MIN_X_WORD = 24,
    RTR_CHUNK_GRID_MIN_Y_WORD = 25,
    RTR_CHUNK_GRID_MIN_Z_WORD = 26,
    RTR_CHUNK_ANGULAR_VELOCITY_X_WORD = 28,
    RTR_CHUNK_ANGULAR_VELOCITY_Y_WORD = 29,
    RTR_CHUNK_ANGULAR_VELOCITY_Z_WORD = 30,
};

enum {
    RTR_CAMERA_AUTO_DEFAULT = 1u,
};

#define RTR_CAMERA_DEFAULT_YAW 0.35f
#define RTR_CAMERA_DEFAULT_PITCH 0.473f
#define RTR_CAMERA_DEFAULT_RADIUS 2.35f
#define RTR_CAMERA_AUTO_SPEED 0.08f
#define RTR_FRAME_TIME_CLAMP_SECONDS (1.0 / 20.0)
#define RTR_SCENE_VOXEL_GRID_X (RTR_SCENE_BRICK_GRID_X * RTR_SCENE_BRICK_SIZE)
#define RTR_SCENE_VOXEL_GRID_Y (RTR_SCENE_BRICK_GRID_Y * RTR_SCENE_BRICK_SIZE)
#define RTR_SCENE_VOXEL_GRID_Z (RTR_SCENE_BRICK_GRID_Z * RTR_SCENE_BRICK_SIZE)
#define RTR_SCENE_VOXEL_SLICE (RTR_SCENE_VOXEL_GRID_X * RTR_SCENE_VOXEL_GRID_Z)
#define RTR_SCENE_VOXEL_COUNT (RTR_SCENE_VOXEL_SLICE * RTR_SCENE_VOXEL_GRID_Y)
#define RTR_SCENE_VOXEL_BRICK_WORD \
    (RTR_MEMORY_VOXEL_MAP_WORD + RTR_SCENE_BRICK_CAPACITY)
#define RTR_CHUNK_MAX 128u
#define RTR_CHUNK_SIZE_X 8u
#define RTR_CHUNK_SIZE_Y 16u
#define RTR_CHUNK_SIZE_Z 8u
#define RTR_CHUNK_GRID_X \
    ((RTR_SCENE_VOXEL_GRID_X + RTR_CHUNK_SIZE_X - 1u) / RTR_CHUNK_SIZE_X)
#define RTR_CHUNK_GRID_Y \
    ((RTR_SCENE_VOXEL_GRID_Y + RTR_CHUNK_SIZE_Y - 1u) / RTR_CHUNK_SIZE_Y)
#define RTR_CHUNK_GRID_Z \
    ((RTR_SCENE_VOXEL_GRID_Z + RTR_CHUNK_SIZE_Z - 1u) / RTR_CHUNK_SIZE_Z)
#define RTR_CHUNK_CELL_COUNT (RTR_CHUNK_GRID_X * RTR_CHUNK_GRID_Y * RTR_CHUNK_GRID_Z)
#define RTR_CHUNK_MASK_BITS (RTR_CHUNK_SIZE_X * RTR_CHUNK_SIZE_Y * RTR_CHUNK_SIZE_Z)
#define RTR_CHUNK_MASK_WORDS ((RTR_CHUNK_MASK_BITS + 31u) / 32u)
#define RTR_CHUNK_STATE_WORDS 32u
#define RTR_SHATTER_TARGET_CHUNKS 96u
#define RTR_SHATTER_MAX_FRAGMENTS 3u
#define RTR_SHATTER_MIN_SPLIT_VOXELS 96u
#define RTR_SHATTER_MIN_FRAGMENT_VOXELS 18u
#define RTR_MEMORY_CHUNK_STATE_WORD (RTR_MEMORY_HEADER_WORDS + RTR_SCENE_WORDS)
#define RTR_MEMORY_CHUNK_MASK_WORD \
    (RTR_MEMORY_CHUNK_STATE_WORD + RTR_CHUNK_MAX * RTR_CHUNK_STATE_WORDS)
#define RTR_CHUNK_WORDS \
    (RTR_CHUNK_MAX * RTR_CHUNK_STATE_WORDS + RTR_CHUNK_MAX * RTR_CHUNK_MASK_WORDS)

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

enum {
    RTR_PIPELINE_SHATTER,
    RTR_PIPELINE_PHYSICS,
    RTR_PIPELINE_RENDER,
    RTR_PIPELINE_COUNT,
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
static VkPipeline rtrPipelines[RTR_PIPELINE_COUNT];
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
static uint32_t rtrCameraAutoReady = 0u;
static uint32_t rtrCameraAutoWasEnabled = 0u;
static float rtrCameraAutoBase = RTR_CAMERA_DEFAULT_YAW;
static uint32_t rtrChunkCount = 0u;
static uint32_t rtrChunkPhysicsActive = 0u;
static float rtrCurrentFrameTime = 0.0f;
static float rtrChunkPhysicsLastTime = 0.0f;

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

typedef struct RTRVec3 {
    float x;
    float y;
    float z;
} RTRVec3;

typedef struct RTRQuat {
    float x;
    float y;
    float z;
    float w;
} RTRQuat;

typedef struct RTRChunk {
    uint32_t active;
    uint32_t awake;
    uint32_t baseX;
    uint32_t baseY;
    uint32_t baseZ;
    uint32_t voxelCount;
    float invMass;
    RTRVec3 position;
    RTRVec3 velocity;
    RTRVec3 angularVelocity;
    RTRVec3 localMin;
    RTRVec3 localMax;
    RTRVec3 gridMin;
    RTRQuat rotation;
} RTRChunk;

static RTRChunk rtrChunks[RTR_CHUNK_MAX];

static float rtrWordF32(uint32_t word)
{
    float value = 0.0f;
    memcpy(&value, &word, sizeof(value));
    return value;
}

static RTRVec3 rtrVec3(float x, float y, float z)
{
    return (RTRVec3){x, y, z};
}

static RTRQuat rtrQuat(float x, float y, float z, float w)
{
    return (RTRQuat){x, y, z, w};
}

static RTRVec3 rtrVec3Add(RTRVec3 a, RTRVec3 b)
{
    return rtrVec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static RTRVec3 rtrVec3Sub(RTRVec3 a, RTRVec3 b)
{
    return rtrVec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static RTRVec3 rtrVec3Mul(RTRVec3 v, float s)
{
    return rtrVec3(v.x * s, v.y * s, v.z * s);
}

static RTRVec3 rtrVec3Div(RTRVec3 a, RTRVec3 b)
{
    return rtrVec3(a.x / b.x, a.y / b.y, a.z / b.z);
}

static float rtrVec3Dot(RTRVec3 a, RTRVec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static RTRVec3 rtrVec3Cross(RTRVec3 a, RTRVec3 b)
{
    return rtrVec3(a.y * b.z - a.z * b.y,
                   a.z * b.x - a.x * b.z,
                   a.x * b.y - a.y * b.x);
}

static RTRVec3 rtrVec3Normalize(RTRVec3 v)
{
    const float len2 = rtrVec3Dot(v, v);
    const float invLen = len2 > 0.0f ? 1.0f / sqrtf(len2) : 0.0f;
    return rtrVec3Mul(v, invLen);
}

static float rtrVec3Length(RTRVec3 v)
{
    return sqrtf(rtrVec3Dot(v, v));
}

static RTRVec3 rtrVec3Min(RTRVec3 a, RTRVec3 b)
{
    return rtrVec3(fminf(a.x, b.x), fminf(a.y, b.y), fminf(a.z, b.z));
}

static RTRVec3 rtrVec3Max(RTRVec3 a, RTRVec3 b)
{
    return rtrVec3(fmaxf(a.x, b.x), fmaxf(a.y, b.y), fmaxf(a.z, b.z));
}

static RTRQuat rtrQuatNormalize(RTRQuat q)
{
    const float len2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    const float invLen = len2 > 0.0f ? 1.0f / sqrtf(len2) : 0.0f;
    return rtrQuat(q.x * invLen, q.y * invLen, q.z * invLen, q.w * invLen);
}

static RTRQuat rtrQuatConjugate(RTRQuat q)
{
    return rtrQuat(-q.x, -q.y, -q.z, q.w);
}

static RTRQuat rtrQuatMul(RTRQuat a, RTRQuat b)
{
    return rtrQuat(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                   a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                   a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                   a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

static RTRVec3 rtrQuatRotate(RTRQuat q, RTRVec3 v)
{
    const RTRVec3 u = rtrVec3(q.x, q.y, q.z);
    const RTRVec3 uv = rtrVec3Cross(u, v);
    const RTRVec3 uuv = rtrVec3Cross(u, uv);
    return rtrVec3Add(v,
                      rtrVec3Add(rtrVec3Mul(uv, 2.0f * q.w),
                                 rtrVec3Mul(uuv, 2.0f)));
}

static RTRVec3 rtrQuatInverseRotate(RTRQuat q, RTRVec3 v)
{
    return rtrQuatRotate(rtrQuatConjugate(q), v);
}

static RTRQuat rtrQuatIntegrate(RTRQuat q, RTRVec3 angularVelocity, float dt)
{
    const RTRQuat omega =
        rtrQuat(angularVelocity.x, angularVelocity.y, angularVelocity.z, 0.0f);
    const RTRQuat dq = rtrQuatMul(omega, q);
    return rtrQuatNormalize(rtrQuat(q.x + dq.x * 0.5f * dt,
                                    q.y + dq.y * 0.5f * dt,
                                    q.z + dq.z * 0.5f * dt,
                                    q.w + dq.w * 0.5f * dt));
}

static uint32_t rtrBrickMapIndex(uint32_t bx, uint32_t by, uint32_t bz)
{
    return (by * RTR_SCENE_BRICK_GRID_Z + bz) * RTR_SCENE_BRICK_GRID_X + bx;
}

static uint32_t rtrVoxelInBounds(int32_t x, int32_t y, int32_t z)
{
    return x >= 0 && y >= 0 && z >= 0 &&
        x < (int32_t)RTR_SCENE_VOXEL_GRID_X &&
        y < (int32_t)RTR_SCENE_VOXEL_GRID_Y &&
        z < (int32_t)RTR_SCENE_VOXEL_GRID_Z;
}

static uint32_t rtrSceneBrickMapRef(uint32_t bx, uint32_t by, uint32_t bz)
{
    return rtrMemoryWords[RTR_MEMORY_VOXEL_MAP_WORD + rtrBrickMapIndex(bx, by, bz)];
}

static uint32_t rtrSceneVoxelIndex(uint32_t x, uint32_t y, uint32_t z)
{
    return (y * RTR_SCENE_VOXEL_GRID_Z + z) * RTR_SCENE_VOXEL_GRID_X + x;
}

static uint32_t rtrSceneBrickWord(uint32_t ref)
{
    return RTR_SCENE_VOXEL_BRICK_WORD + (ref - 1u) * RTR_SCENE_BRICK_WORDS;
}

static void rtrSceneVoxelClear(int32_t x, int32_t y, int32_t z)
{
    if (!rtrVoxelInBounds(x, y, z)) return;

    const uint32_t bx = (uint32_t)x / RTR_SCENE_BRICK_SIZE;
    const uint32_t by = (uint32_t)y / RTR_SCENE_BRICK_SIZE;
    const uint32_t bz = (uint32_t)z / RTR_SCENE_BRICK_SIZE;
    const uint32_t mapWord = RTR_MEMORY_VOXEL_MAP_WORD + rtrBrickMapIndex(bx, by, bz);
    const uint32_t ref = rtrMemoryWords[mapWord];
    const uint32_t lx = (uint32_t)x & (RTR_SCENE_BRICK_SIZE - 1u);
    const uint32_t ly = (uint32_t)y & (RTR_SCENE_BRICK_SIZE - 1u);
    const uint32_t lz = (uint32_t)z & (RTR_SCENE_BRICK_SIZE - 1u);
    const uint32_t bit = lx + lz * 4u + ly * 16u;

    if (ref == 0u || ref > rtrMemoryWords[RTR_MEMORY_BRICK_COUNT_WORD])
        return;
    const uint32_t word = rtrSceneBrickWord(ref);

    rtrMemoryWords[word + (bit >> 5u)] &= ~(1u << (bit & 31u));
    if ((rtrMemoryWords[word] | rtrMemoryWords[word + 1u]) == 0u)
        rtrMemoryWords[mapWord] = 0u;
}

static uint32_t rtrSceneLoadOccupancy(uint8_t *occupied)
{
    uint32_t brickCount = rtrMemoryWords[RTR_MEMORY_BRICK_COUNT_WORD];
    uint32_t occupiedCount = 0u;

    if (brickCount > RTR_SCENE_BRICK_CAPACITY)
        brickCount = RTR_SCENE_BRICK_CAPACITY;
    memset(occupied, 0, RTR_SCENE_VOXEL_COUNT);

    for (uint32_t by = 0u; by < RTR_SCENE_BRICK_GRID_Y; by++) {
        for (uint32_t bz = 0u; bz < RTR_SCENE_BRICK_GRID_Z; bz++) {
            for (uint32_t bx = 0u; bx < RTR_SCENE_BRICK_GRID_X; bx++) {
                const uint32_t ref = rtrSceneBrickMapRef(bx, by, bz);
                if (ref == 0u || ref > brickCount) continue;

                const uint32_t word = rtrSceneBrickWord(ref);
                const uint32_t masks[2] = {
                    rtrMemoryWords[word],
                    rtrMemoryWords[word + 1u],
                };

                for (uint32_t bit = 0u; bit < 64u; bit++) {
                    const uint32_t mask = masks[bit >> 5u];
                    if ((mask & (1u << (bit & 31u))) == 0u) continue;

                    const uint32_t lx = bit & 3u;
                    const uint32_t lz = (bit >> 2u) & 3u;
                    const uint32_t ly = (bit >> 4u) & 3u;
                    const uint32_t x = bx * RTR_SCENE_BRICK_SIZE + lx;
                    const uint32_t y = by * RTR_SCENE_BRICK_SIZE + ly;
                    const uint32_t z = bz * RTR_SCENE_BRICK_SIZE + lz;
                    const uint32_t index = rtrSceneVoxelIndex(x, y, z);

                    if (!occupied[index]) {
                        occupied[index] = 1u;
                        occupiedCount++;
                    }
                }
            }
        }
    }

    return occupiedCount;
}

static RTRVec3 rtrSceneMin(void)
{
    return rtrVec3(rtrWordF32(rtrMemoryWords[RTR_MEMORY_SCENE_MIN_WORD]),
                   rtrWordF32(rtrMemoryWords[RTR_MEMORY_SCENE_MIN_WORD + 1u]),
                   rtrWordF32(rtrMemoryWords[RTR_MEMORY_SCENE_MIN_WORD + 2u]));
}

static RTRVec3 rtrSceneMax(void)
{
    return rtrVec3(rtrWordF32(rtrMemoryWords[RTR_MEMORY_SCENE_MAX_WORD]),
                   rtrWordF32(rtrMemoryWords[RTR_MEMORY_SCENE_MAX_WORD + 1u]),
                   rtrWordF32(rtrMemoryWords[RTR_MEMORY_SCENE_MAX_WORD + 2u]));
}

static RTRVec3 rtrSceneVoxelSize(void)
{
    return rtrVec3Div(rtrVec3Sub(rtrSceneMax(), rtrSceneMin()),
                      rtrVec3((float)RTR_SCENE_VOXEL_GRID_X,
                              (float)RTR_SCENE_VOXEL_GRID_Y,
                              (float)RTR_SCENE_VOXEL_GRID_Z));
}

static RTRVec3 rtrRayForPixelCPU(float pixelX,
                                 float pixelY,
                                 float cameraYaw,
                                 float cameraPitch,
                                 float cameraRadius,
                                 RTRVec3 *outRo);

static uint32_t rtrBoxHitCPU(RTRVec3 ro,
                             RTRVec3 rd,
                             RTRVec3 bmin,
                             RTRVec3 bmax,
                             float *outT0,
                             float *outT1)
{
    const RTRVec3 safe = rtrVec3(fmaxf(fabsf(rd.x), 1.0e-8f),
                                 fmaxf(fabsf(rd.y), 1.0e-8f),
                                 fmaxf(fabsf(rd.z), 1.0e-8f));
    const RTRVec3 inv = rtrVec3(1.0f / (rd.x >= 0.0f ? safe.x : -safe.x),
                                1.0f / (rd.y >= 0.0f ? safe.y : -safe.y),
                                1.0f / (rd.z >= 0.0f ? safe.z : -safe.z));
    const RTRVec3 a = rtrVec3Sub(bmin, ro);
    const RTRVec3 b = rtrVec3Sub(bmax, ro);
    const RTRVec3 t0 = rtrVec3(a.x * inv.x, a.y * inv.y, a.z * inv.z);
    const RTRVec3 t1 = rtrVec3(b.x * inv.x, b.y * inv.y, b.z * inv.z);
    const RTRVec3 lo = rtrVec3Min(t0, t1);
    const RTRVec3 hi = rtrVec3Max(t0, t1);
    const float nearT = fmaxf(fmaxf(lo.x, lo.y), fmaxf(lo.z, 0.0001f));
    const float farT = fminf(fminf(hi.x, hi.y), hi.z);

    if (outT0) *outT0 = nearT;
    if (outT1) *outT1 = farT;
    return (uint32_t)(nearT <= farT);
}

static uint32_t rtrChunkStateWord(uint32_t chunk, uint32_t word)
{
    return RTR_MEMORY_CHUNK_STATE_WORD + chunk * RTR_CHUNK_STATE_WORDS + word;
}

static uint32_t rtrChunkMaskWord(uint32_t chunk)
{
    return RTR_MEMORY_CHUNK_MASK_WORD + chunk * RTR_CHUNK_MASK_WORDS;
}

static void rtrStoreChunkState(uint32_t index)
{
    const RTRChunk *chunk = &rtrChunks[index];
    const uint32_t base = rtrChunkStateWord(index, 0u);

    rtrMemoryWords[base + RTR_CHUNK_ACTIVE_WORD] = chunk->active;
    rtrMemoryWords[base + RTR_CHUNK_BASE_X_WORD] = chunk->baseX;
    rtrMemoryWords[base + RTR_CHUNK_BASE_Y_WORD] = chunk->baseY;
    rtrMemoryWords[base + RTR_CHUNK_BASE_Z_WORD] = chunk->baseZ;
    rtrMemoryWords[base + RTR_CHUNK_POSITION_X_WORD] = rtrF32Word(chunk->position.x);
    rtrMemoryWords[base + RTR_CHUNK_POSITION_Y_WORD] = rtrF32Word(chunk->position.y);
    rtrMemoryWords[base + RTR_CHUNK_POSITION_Z_WORD] = rtrF32Word(chunk->position.z);
    rtrMemoryWords[base + RTR_CHUNK_AWAKE_WORD] = chunk->awake;
    rtrMemoryWords[base + RTR_CHUNK_ROTATION_X_WORD] = rtrF32Word(chunk->rotation.x);
    rtrMemoryWords[base + RTR_CHUNK_ROTATION_Y_WORD] = rtrF32Word(chunk->rotation.y);
    rtrMemoryWords[base + RTR_CHUNK_ROTATION_Z_WORD] = rtrF32Word(chunk->rotation.z);
    rtrMemoryWords[base + RTR_CHUNK_ROTATION_W_WORD] = rtrF32Word(chunk->rotation.w);
    rtrMemoryWords[base + RTR_CHUNK_VOXEL_COUNT_WORD] = chunk->voxelCount;
    rtrMemoryWords[base + RTR_CHUNK_VELOCITY_X_WORD] = rtrF32Word(chunk->velocity.x);
    rtrMemoryWords[base + RTR_CHUNK_VELOCITY_Y_WORD] = rtrF32Word(chunk->velocity.y);
    rtrMemoryWords[base + RTR_CHUNK_VELOCITY_Z_WORD] = rtrF32Word(chunk->velocity.z);
    rtrMemoryWords[base + RTR_CHUNK_LOCAL_MIN_X_WORD] = rtrF32Word(chunk->localMin.x);
    rtrMemoryWords[base + RTR_CHUNK_LOCAL_MIN_Y_WORD] = rtrF32Word(chunk->localMin.y);
    rtrMemoryWords[base + RTR_CHUNK_LOCAL_MIN_Z_WORD] = rtrF32Word(chunk->localMin.z);
    rtrMemoryWords[base + RTR_CHUNK_INV_MASS_WORD] = rtrF32Word(chunk->invMass);
    rtrMemoryWords[base + RTR_CHUNK_LOCAL_MAX_X_WORD] = rtrF32Word(chunk->localMax.x);
    rtrMemoryWords[base + RTR_CHUNK_LOCAL_MAX_Y_WORD] = rtrF32Word(chunk->localMax.y);
    rtrMemoryWords[base + RTR_CHUNK_LOCAL_MAX_Z_WORD] = rtrF32Word(chunk->localMax.z);
    rtrMemoryWords[base + RTR_CHUNK_GRID_MIN_X_WORD] = rtrF32Word(chunk->gridMin.x);
    rtrMemoryWords[base + RTR_CHUNK_GRID_MIN_Y_WORD] = rtrF32Word(chunk->gridMin.y);
    rtrMemoryWords[base + RTR_CHUNK_GRID_MIN_Z_WORD] = rtrF32Word(chunk->gridMin.z);
    rtrMemoryWords[base + RTR_CHUNK_ANGULAR_VELOCITY_X_WORD] =
        rtrF32Word(chunk->angularVelocity.x);
    rtrMemoryWords[base + RTR_CHUNK_ANGULAR_VELOCITY_Y_WORD] =
        rtrF32Word(chunk->angularVelocity.y);
    rtrMemoryWords[base + RTR_CHUNK_ANGULAR_VELOCITY_Z_WORD] =
        rtrF32Word(chunk->angularVelocity.z);
}

static void rtrStoreAllChunkStates(void)
{
    rtrMemoryWords[RTR_MEMORY_CHUNK_COUNT_WORD] = rtrChunkCount;
    for (uint32_t i = 0u; i < rtrChunkCount; i++)
        rtrStoreChunkState(i);
}

static uint32_t rtrHashU32(uint32_t x)
{
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

static uint32_t rtrHash4U32(uint32_t x, uint32_t y, uint32_t z, uint32_t salt)
{
    return rtrHashU32(x * 0x9e3779b9u ^
                      y * 0x85ebca6bu ^
                      z * 0xc2b2ae35u ^
                      salt);
}

static uint32_t rtrChunkCellIndex(uint32_t cx, uint32_t cy, uint32_t cz)
{
    return (cy * RTR_CHUNK_GRID_Z + cz) * RTR_CHUNK_GRID_X + cx;
}

static uint32_t rtrChunkLocalIndex(uint32_t lx, uint32_t ly, uint32_t lz)
{
    return (ly * RTR_CHUNK_SIZE_Z + lz) * RTR_CHUNK_SIZE_X + lx;
}

static void rtrChunkLocalCoords(uint32_t index,
                                uint32_t *lx,
                                uint32_t *ly,
                                uint32_t *lz)
{
    *lx = index % RTR_CHUNK_SIZE_X;
    index /= RTR_CHUNK_SIZE_X;
    *lz = index % RTR_CHUNK_SIZE_Z;
    *ly = index / RTR_CHUNK_SIZE_Z;
}

static uint32_t rtrChunkLocalNeighbor(uint32_t index,
                                      uint32_t direction,
                                      uint16_t *neighbor)
{
    uint32_t lx = 0u;
    uint32_t ly = 0u;
    uint32_t lz = 0u;
    rtrChunkLocalCoords(index, &lx, &ly, &lz);

    switch (direction) {
    case 0u:
        if (lx + 1u >= RTR_CHUNK_SIZE_X) return 0u;
        lx++;
        break;
    case 1u:
        if (lx == 0u) return 0u;
        lx--;
        break;
    case 2u:
        if (ly + 1u >= RTR_CHUNK_SIZE_Y) return 0u;
        ly++;
        break;
    case 3u:
        if (ly == 0u) return 0u;
        ly--;
        break;
    case 4u:
        if (lz + 1u >= RTR_CHUNK_SIZE_Z) return 0u;
        lz++;
        break;
    default:
        if (lz == 0u) return 0u;
        lz--;
        break;
    }

    *neighbor = (uint16_t)rtrChunkLocalIndex(lx, ly, lz);
    return 1u;
}

static uint32_t rtrChunkLocalDistance2(uint32_t a, uint32_t b)
{
    uint32_t ax = 0u;
    uint32_t ay = 0u;
    uint32_t az = 0u;
    uint32_t bx = 0u;
    uint32_t by = 0u;
    uint32_t bz = 0u;
    rtrChunkLocalCoords(a, &ax, &ay, &az);
    rtrChunkLocalCoords(b, &bx, &by, &bz);

    const int32_t dx = (int32_t)ax - (int32_t)bx;
    const int32_t dy = (int32_t)ay - (int32_t)by;
    const int32_t dz = (int32_t)az - (int32_t)bz;
    return (uint32_t)(dx * dx + dy * dy + dz * dz);
}

static void rtrPickShatterSeeds(const uint16_t *occupied,
                                uint32_t voxelCount,
                                uint32_t fragmentCount,
                                uint32_t seedHash,
                                uint16_t *seeds)
{
    seeds[0] = occupied[seedHash % voxelCount];

    for (uint32_t fragment = 1u; fragment < fragmentCount; fragment++) {
        uint32_t bestScore = 0u;
        uint16_t bestVoxel = occupied[0];

        for (uint32_t i = 0u; i < voxelCount; i++) {
            const uint16_t candidate = occupied[i];
            uint32_t minDistance = UINT32_MAX;

            for (uint32_t seed = 0u; seed < fragment; seed++) {
                const uint32_t distance =
                    rtrChunkLocalDistance2(candidate, seeds[seed]);
                if (distance < minDistance) minDistance = distance;
            }

            const uint32_t jitter =
                rtrHashU32(seedHash ^ candidate ^ (fragment * 0x27d4eb2du)) & 63u;
            const uint32_t score = minDistance * 64u + jitter;
            if (score > bestScore) {
                bestScore = score;
                bestVoxel = candidate;
            }
        }

        seeds[fragment] = bestVoxel;
    }
}

static void rtrAssignShatterOwners(const uint16_t *occupied,
                                   uint32_t voxelCount,
                                   uint32_t fragmentCount,
                                   uint32_t seedHash,
                                   uint8_t *owners)
{
    uint8_t present[RTR_CHUNK_MASK_BITS];
    uint16_t seeds[RTR_SHATTER_MAX_FRAGMENTS];
    uint16_t queue[RTR_CHUNK_MASK_BITS];
    uint8_t queueOwner[RTR_CHUNK_MASK_BITS];
    uint32_t head = 0u;
    uint32_t tail = 0u;

    memset(present, 0, sizeof(present));
    memset(owners, 0xff, RTR_CHUNK_MASK_BITS);

    for (uint32_t i = 0u; i < voxelCount; i++)
        present[occupied[i]] = 1u;

    if (fragmentCount <= 1u) {
        for (uint32_t i = 0u; i < voxelCount; i++)
            owners[occupied[i]] = 0u;
        return;
    }

    rtrPickShatterSeeds(occupied, voxelCount, fragmentCount, seedHash, seeds);
    for (uint32_t fragment = 0u; fragment < fragmentCount; fragment++) {
        const uint16_t seed = seeds[fragment];
        if (owners[seed] != 0xffu) continue;
        owners[seed] = (uint8_t)fragment;
        queue[tail] = seed;
        queueOwner[tail] = (uint8_t)fragment;
        tail++;
    }

    while (head < tail) {
        const uint16_t current = queue[head];
        const uint8_t owner = queueOwner[head];
        const uint32_t startDir =
            rtrHashU32(seedHash ^ current ^ ((uint32_t)owner * 0x9e3779b9u)) % 6u;
        head++;

        for (uint32_t step = 0u; step < 6u; step++) {
            uint16_t neighbor = 0u;
            const uint32_t direction = (startDir + step) % 6u;
            if (!rtrChunkLocalNeighbor(current, direction, &neighbor))
                continue;
            if (!present[neighbor] || owners[neighbor] != 0xffu)
                continue;

            owners[neighbor] = owner;
            queue[tail] = neighbor;
            queueOwner[tail] = owner;
            tail++;
        }
    }

    for (uint32_t pass = 0u; pass < RTR_CHUNK_MASK_BITS; pass++) {
        uint32_t progress = 0u;

        for (uint32_t i = 0u; i < voxelCount; i++) {
            const uint16_t current = occupied[i];
            if (owners[current] != 0xffu) continue;

            const uint32_t startDir =
                rtrHashU32(seedHash ^ current ^ (pass * 0x45d9f3bu)) % 6u;
            for (uint32_t step = 0u; step < 6u; step++) {
                uint16_t neighbor = 0u;
                const uint32_t direction = (startDir + step) % 6u;
                if (!rtrChunkLocalNeighbor(current, direction, &neighbor))
                    continue;
                if (owners[neighbor] == 0xffu)
                    continue;

                owners[current] = owners[neighbor];
                progress = 1u;
                break;
            }
        }

        if (!progress) break;
    }

    for (uint32_t i = 0u; i < voxelCount; i++) {
        const uint16_t current = occupied[i];
        if (owners[current] != 0xffu) continue;

        uint32_t bestDistance = UINT32_MAX;
        uint8_t bestOwner = 0u;
        for (uint32_t fragment = 0u; fragment < fragmentCount; fragment++) {
            const uint32_t distance =
                rtrChunkLocalDistance2(current, seeds[fragment]);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestOwner = (uint8_t)fragment;
            }
        }
        owners[current] = bestOwner;
    }
}

static void rtrMergeSmallShatterFragments(const uint16_t *occupied,
                                          uint32_t voxelCount,
                                          uint32_t fragmentCount,
                                          uint8_t *owners)
{
    for (;;) {
        uint32_t counts[RTR_SHATTER_MAX_FRAGMENTS] = {0u};
        uint32_t smallestCount = UINT32_MAX;
        uint32_t smallest = UINT32_MAX;
        uint32_t largestCount = 0u;
        uint32_t largest = UINT32_MAX;

        for (uint32_t i = 0u; i < voxelCount; i++) {
            const uint8_t owner = owners[occupied[i]];
            if (owner < fragmentCount)
                counts[owner]++;
        }

        for (uint32_t fragment = 0u; fragment < fragmentCount; fragment++) {
            if (counts[fragment] &&
                counts[fragment] < RTR_SHATTER_MIN_FRAGMENT_VOXELS &&
                counts[fragment] < smallestCount) {
                smallestCount = counts[fragment];
                smallest = fragment;
            }
            if (counts[fragment] > largestCount) {
                largestCount = counts[fragment];
                largest = fragment;
            }
        }

        if (smallest == UINT32_MAX || largest == UINT32_MAX || smallest == largest)
            break;

        for (uint32_t i = 0u; i < voxelCount; i++) {
            const uint16_t voxel = occupied[i];
            if (owners[voxel] == (uint8_t)smallest)
                owners[voxel] = (uint8_t)largest;
        }
    }
}

static void rtrPlanShatterFragments(const uint16_t *macroCounts,
                                    uint8_t *macroDesired,
                                    uint32_t activeCells)
{
    const uint32_t target =
        RTR_SHATTER_TARGET_CHUNKS < RTR_CHUNK_MAX ?
            RTR_SHATTER_TARGET_CHUNKS :
            RTR_CHUNK_MAX;
    uint32_t extraBudget = target > activeCells ? target - activeCells : 0u;

    memset(macroDesired, 0, RTR_CHUNK_CELL_COUNT);
    for (uint32_t i = 0u; i < RTR_CHUNK_CELL_COUNT; i++) {
        if (macroCounts[i])
            macroDesired[i] = 1u;
    }

    while (extraBudget) {
        uint32_t bestCell = UINT32_MAX;
        uint32_t bestScore = 0u;

        for (uint32_t i = 0u; i < RTR_CHUNK_CELL_COUNT; i++) {
            const uint32_t desired = macroDesired[i];
            if (!desired ||
                desired >= RTR_SHATTER_MAX_FRAGMENTS ||
                macroCounts[i] < RTR_SHATTER_MIN_SPLIT_VOXELS) {
                continue;
            }

            const uint32_t splitPenalty = desired * desired;
            const uint32_t score =
                ((uint32_t)macroCounts[i] * 1024u) / splitPenalty +
                (rtrHashU32(i ^ 0x6d2b79f5u) & 255u);
            if (score > bestScore) {
                bestScore = score;
                bestCell = i;
            }
        }

        if (bestCell == UINT32_MAX)
            break;

        macroDesired[bestCell]++;
        extraBudget--;
    }
}

static void RTR_UNUSED rtrBuildVoxelChunks(void)
{
    uint8_t *occupied = (uint8_t *)malloc(RTR_SCENE_VOXEL_COUNT);
    const RTRVec3 sceneMin = rtrSceneMin();
    const RTRVec3 voxelSize = rtrSceneVoxelSize();
    uint16_t macroCounts[RTR_CHUNK_CELL_COUNT];
    uint8_t macroDesired[RTR_CHUNK_CELL_COUNT];
    uint32_t activeCells = 0u;

    rtrChunkCount = 0u;
    rtrChunkPhysicsActive = 0u;
    memset(rtrChunks, 0, sizeof(rtrChunks));
    memset(macroCounts, 0, sizeof(macroCounts));
    memset(macroDesired, 0, sizeof(macroDesired));
    memset(rtrMemoryWords + RTR_MEMORY_CHUNK_STATE_WORD,
           0,
           RTR_CHUNK_WORDS * sizeof(uint32_t));

    if (!occupied)
        return;

    rtrSceneLoadOccupancy(occupied);

    for (uint32_t cy = 0u; cy < RTR_CHUNK_GRID_Y; cy++) {
        const uint32_t by = cy * RTR_CHUNK_SIZE_Y;
        for (uint32_t cz = 0u; cz < RTR_CHUNK_GRID_Z; cz++) {
            const uint32_t bz = cz * RTR_CHUNK_SIZE_Z;
            for (uint32_t cx = 0u; cx < RTR_CHUNK_GRID_X; cx++) {
                const uint32_t bx = cx * RTR_CHUNK_SIZE_X;
                uint32_t voxelCount = 0u;

                for (uint32_t ly = 0u; ly < RTR_CHUNK_SIZE_Y; ly++) {
                    for (uint32_t lz = 0u; lz < RTR_CHUNK_SIZE_Z; lz++) {
                        for (uint32_t lx = 0u; lx < RTR_CHUNK_SIZE_X; lx++) {
                            const uint32_t x = bx + lx;
                            const uint32_t y = by + ly;
                            const uint32_t z = bz + lz;
                            if (x >= RTR_SCENE_VOXEL_GRID_X ||
                                y >= RTR_SCENE_VOXEL_GRID_Y ||
                                z >= RTR_SCENE_VOXEL_GRID_Z ||
                                y == 0u) {
                                continue;
                            }
                            if (occupied[rtrSceneVoxelIndex(x, y, z)])
                                voxelCount++;
                        }
                    }
                }

                if (voxelCount) {
                    const uint32_t cell = rtrChunkCellIndex(cx, cy, cz);
                    macroCounts[cell] = (uint16_t)voxelCount;
                    activeCells++;
                }
            }
        }
    }

    rtrPlanShatterFragments(macroCounts, macroDesired, activeCells);

    for (uint32_t cy = 0u; cy < RTR_CHUNK_GRID_Y; cy++) {
        const uint32_t by = cy * RTR_CHUNK_SIZE_Y;
        for (uint32_t cz = 0u; cz < RTR_CHUNK_GRID_Z; cz++) {
            const uint32_t bz = cz * RTR_CHUNK_SIZE_Z;
            for (uint32_t cx = 0u; cx < RTR_CHUNK_GRID_X; cx++) {
                const uint32_t bx = cx * RTR_CHUNK_SIZE_X;
                const uint32_t cell = rtrChunkCellIndex(cx, cy, cz);
                uint16_t occupiedLocal[RTR_CHUNK_MASK_BITS];
                uint8_t owners[RTR_CHUNK_MASK_BITS];
                uint32_t masks[RTR_SHATTER_MAX_FRAGMENTS][RTR_CHUNK_MASK_WORDS];
                uint32_t counts[RTR_SHATTER_MAX_FRAGMENTS] = {0u};
                uint32_t minLx[RTR_SHATTER_MAX_FRAGMENTS];
                uint32_t minLy[RTR_SHATTER_MAX_FRAGMENTS];
                uint32_t minLz[RTR_SHATTER_MAX_FRAGMENTS];
                uint32_t maxLx[RTR_SHATTER_MAX_FRAGMENTS] = {0u};
                uint32_t maxLy[RTR_SHATTER_MAX_FRAGMENTS] = {0u};
                uint32_t maxLz[RTR_SHATTER_MAX_FRAGMENTS] = {0u};
                uint32_t sumLx[RTR_SHATTER_MAX_FRAGMENTS] = {0u};
                uint32_t sumLy[RTR_SHATTER_MAX_FRAGMENTS] = {0u};
                uint32_t sumLz[RTR_SHATTER_MAX_FRAGMENTS] = {0u};
                uint32_t localVoxelCount = 0u;
                uint32_t fragmentCount = macroDesired[cell];

                if (!fragmentCount)
                    continue;

                for (uint32_t fragment = 0u;
                     fragment < RTR_SHATTER_MAX_FRAGMENTS;
                     fragment++) {
                    minLx[fragment] = RTR_CHUNK_SIZE_X;
                    minLy[fragment] = RTR_CHUNK_SIZE_Y;
                    minLz[fragment] = RTR_CHUNK_SIZE_Z;
                }
                memset(masks, 0, sizeof(masks));

                for (uint32_t ly = 0u; ly < RTR_CHUNK_SIZE_Y; ly++) {
                    for (uint32_t lz = 0u; lz < RTR_CHUNK_SIZE_Z; lz++) {
                        for (uint32_t lx = 0u; lx < RTR_CHUNK_SIZE_X; lx++) {
                            const uint32_t x = bx + lx;
                            const uint32_t y = by + ly;
                            const uint32_t z = bz + lz;
                            if (x >= RTR_SCENE_VOXEL_GRID_X ||
                                y >= RTR_SCENE_VOXEL_GRID_Y ||
                                z >= RTR_SCENE_VOXEL_GRID_Z ||
                                y == 0u) {
                                continue;
                            }
                            if (!occupied[rtrSceneVoxelIndex(x, y, z)])
                                continue;

                            occupiedLocal[localVoxelCount++] =
                                (uint16_t)rtrChunkLocalIndex(lx, ly, lz);
                        }
                    }
                }

                if (!localVoxelCount)
                    continue;
                if (fragmentCount > localVoxelCount)
                    fragmentCount = localVoxelCount;

                rtrAssignShatterOwners(occupiedLocal,
                                       localVoxelCount,
                                       fragmentCount,
                                       rtrHash4U32(cx, cy, cz, 0x51ed270bu),
                                       owners);
                rtrMergeSmallShatterFragments(occupiedLocal,
                                              localVoxelCount,
                                              fragmentCount,
                                              owners);

                for (uint32_t i = 0u; i < localVoxelCount; i++) {
                    const uint16_t bit = occupiedLocal[i];
                    uint32_t lx = 0u;
                    uint32_t ly = 0u;
                    uint32_t lz = 0u;
                    uint32_t owner = owners[bit];

                    if (owner >= fragmentCount)
                        owner = 0u;
                    rtrChunkLocalCoords(bit, &lx, &ly, &lz);
                    masks[owner][bit >> 5u] |= 1u << (bit & 31u);
                    counts[owner]++;
                    sumLx[owner] += lx;
                    sumLy[owner] += ly;
                    sumLz[owner] += lz;
                    if (lx < minLx[owner]) minLx[owner] = lx;
                    if (ly < minLy[owner]) minLy[owner] = ly;
                    if (lz < minLz[owner]) minLz[owner] = lz;
                    if (lx > maxLx[owner]) maxLx[owner] = lx;
                    if (ly > maxLy[owner]) maxLy[owner] = ly;
                    if (lz > maxLz[owner]) maxLz[owner] = lz;
                }

                for (uint32_t fragment = 0u; fragment < fragmentCount; fragment++) {
                    if (!counts[fragment])
                        continue;
                    if (rtrChunkCount >= RTR_CHUNK_MAX) {
                        fprintf(stderr,
                                "chunks: capacity reached, dropping remaining shatter fragments\n");
                        goto finish;
                    }

                    const RTRVec3 half =
                        rtrVec3((float)RTR_CHUNK_SIZE_X * voxelSize.x * 0.5f,
                                (float)RTR_CHUNK_SIZE_Y * voxelSize.y * 0.5f,
                                (float)RTR_CHUNK_SIZE_Z * voxelSize.z * 0.5f);
                    const RTRVec3 macroCenter =
                        rtrVec3(sceneMin.x + ((float)bx + (float)RTR_CHUNK_SIZE_X * 0.5f) * voxelSize.x,
                                sceneMin.y + ((float)by + (float)RTR_CHUNK_SIZE_Y * 0.5f) * voxelSize.y,
                                sceneMin.z + ((float)bz + (float)RTR_CHUNK_SIZE_Z * 0.5f) * voxelSize.z);
                    const float invCount = 1.0f / (float)counts[fragment];
                    const RTRVec3 pivot =
                        rtrVec3(-half.x + ((float)sumLx[fragment] * invCount + 0.5f) * voxelSize.x,
                                -half.y + ((float)sumLy[fragment] * invCount + 0.5f) * voxelSize.y,
                                -half.z + ((float)sumLz[fragment] * invCount + 0.5f) * voxelSize.z);

                    RTRChunk *chunk = &rtrChunks[rtrChunkCount];
                    chunk->active = 1u;
                    chunk->awake = 0u;
                    chunk->baseX = bx;
                    chunk->baseY = by;
                    chunk->baseZ = bz;
                    chunk->voxelCount = counts[fragment];
                    chunk->invMass = 96.0f / fmaxf((float)counts[fragment], 96.0f);
                    chunk->position = rtrVec3Add(macroCenter, pivot);
                    chunk->velocity = rtrVec3(0.0f, 0.0f, 0.0f);
                    chunk->angularVelocity = rtrVec3(0.0f, 0.0f, 0.0f);
                    chunk->gridMin =
                        rtrVec3(-half.x - pivot.x,
                                -half.y - pivot.y,
                                -half.z - pivot.z);
                    chunk->localMin =
                        rtrVec3(chunk->gridMin.x + (float)minLx[fragment] * voxelSize.x,
                                chunk->gridMin.y + (float)minLy[fragment] * voxelSize.y,
                                chunk->gridMin.z + (float)minLz[fragment] * voxelSize.z);
                    chunk->localMax =
                        rtrVec3(chunk->gridMin.x + ((float)maxLx[fragment] + 1.0f) * voxelSize.x,
                                chunk->gridMin.y + ((float)maxLy[fragment] + 1.0f) * voxelSize.y,
                                chunk->gridMin.z + ((float)maxLz[fragment] + 1.0f) * voxelSize.z);
                    chunk->rotation = rtrQuat(0.0f, 0.0f, 0.0f, 1.0f);

                    memcpy(rtrMemoryWords + rtrChunkMaskWord(rtrChunkCount),
                           masks[fragment],
                           sizeof(masks[fragment]));
                    rtrChunkCount++;
                }
            }
        }
    }

finish:
    for (uint32_t y = 1u; y < RTR_SCENE_VOXEL_GRID_Y; y++) {
        for (uint32_t z = 0u; z < RTR_SCENE_VOXEL_GRID_Z; z++) {
            for (uint32_t x = 0u; x < RTR_SCENE_VOXEL_GRID_X; x++) {
                if (occupied[rtrSceneVoxelIndex(x, y, z)])
                    rtrSceneVoxelClear((int32_t)x, (int32_t)y, (int32_t)z);
            }
        }
    }

    rtrStoreAllChunkStates();
    fprintf(stderr,
            "chunks: %u irregular rigid voxel chunks from %u macro cells\n",
            rtrChunkCount,
            activeCells);
    free(occupied);
}

static uint32_t rtrChunkBoxHit(const RTRChunk *chunk,
                               RTRVec3 ro,
                               RTRVec3 rd,
                               float *outT)
{
    const RTRVec3 localRo =
        rtrQuatInverseRotate(chunk->rotation, rtrVec3Sub(ro, chunk->position));
    const RTRVec3 localRd = rtrQuatInverseRotate(chunk->rotation, rd);
    float t0 = 0.0f;
    float t1 = 0.0f;

    if (!rtrBoxHitCPU(localRo, localRd, chunk->localMin, chunk->localMax, &t0, &t1))
        return 0u;
    if (outT) *outT = t0;
    return 1u;
}

static float rtrClamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static void rtrWakeChunkPhysics(void)
{
    rtrChunkPhysicsActive = 1u;
    rtrChunkPhysicsLastTime = rtrCurrentFrameTime;
}

static uint32_t rtrApplyImpulseAtPoint(RTRVec3 hitPoint, RTRVec3 rayDir)
{
    const float radius = 3.15f;
    const float strength = 11.5f;
    const float angularStrength = 5.5f;
    uint32_t affected = 0u;

    for (uint32_t i = 0u; i < rtrChunkCount; i++) {
        RTRChunk *chunk = &rtrChunks[i];
        if (!chunk->active) continue;

        const RTRVec3 toChunk = rtrVec3Sub(chunk->position, hitPoint);
        const float dist = rtrVec3Length(toChunk);
        float falloff = 1.0f - dist / radius;
        if (falloff <= 0.0f)
            continue;
        falloff = rtrClamp01(falloff);

        const float jitter = (float)((i * 1103515245u + 12345u) & 1023u) / 1023.0f;
        const RTRVec3 randomTilt =
            rtrVec3(sinf(jitter * 17.0f), 0.35f, cosf(jitter * 23.0f));
        const RTRVec3 dir =
            rtrVec3Normalize(rtrVec3Add(rtrVec3Add(rtrVec3Mul(toChunk, 1.35f),
                                                   rtrVec3Mul(rayDir, 0.85f)),
                                        randomTilt));
        const RTRVec3 impulse = rtrVec3Mul(dir, strength * falloff);
        const RTRVec3 arm =
            rtrVec3Add(rtrVec3Mul(rtrVec3Normalize(toChunk), -0.28f),
                       rtrVec3Mul(randomTilt, 0.10f));
        const RTRVec3 torque = rtrVec3Cross(arm, impulse);

        chunk->velocity =
            rtrVec3Add(chunk->velocity, rtrVec3Mul(impulse, chunk->invMass));
        chunk->angularVelocity =
            rtrVec3Add(chunk->angularVelocity,
                       rtrVec3Mul(torque, angularStrength * chunk->invMass));
        chunk->awake = 1u;
        affected++;
    }

    if (affected)
        rtrWakeChunkPhysics();
    return affected;
}

static uint32_t rtrApplyImpulseRay(float pixelX,
                                   float pixelY,
                                   float cameraYaw,
                                   float cameraPitch,
                                   float cameraRadius)
{
    RTRVec3 ro;
    const RTRVec3 rd = rtrRayForPixelCPU(pixelX,
                                         pixelY,
                                         cameraYaw,
                                         cameraPitch,
                                         cameraRadius,
                                         &ro);
    float bestT = 1.0e20f;
    uint32_t hit = 0u;

    for (uint32_t i = 0u; i < rtrChunkCount; i++) {
        float t = 0.0f;
        if (!rtrChunks[i].active || !rtrChunkBoxHit(&rtrChunks[i], ro, rd, &t))
            continue;
        if (t < bestT) {
            bestT = t;
            hit = 1u;
        }
    }

    if (!hit)
        return 0u;

    return rtrApplyImpulseAtPoint(rtrVec3Add(ro, rtrVec3Mul(rd, bestT)), rd);
}

static uint32_t RTR_UNUSED rtrApplyImpulseAtPixel(float pixelX, float pixelY)
{
    return rtrApplyImpulseRay(pixelX,
                              pixelY,
                              rtrWordF32(rtrMemoryWords[RTR_MEMORY_CAMERA_YAW_WORD]),
                              rtrWordF32(rtrMemoryWords[RTR_MEMORY_CAMERA_PITCH_WORD]),
                              rtrWordF32(rtrMemoryWords[RTR_MEMORY_CAMERA_RADIUS_WORD]));
}

static uint32_t rtrChunkFloorCollide(RTRChunk *chunk)
{
    float minY = 1.0e20f;

    for (uint32_t corner = 0u; corner < 8u; corner++) {
        const RTRVec3 local =
            rtrVec3((corner & 1u) ? chunk->localMax.x : chunk->localMin.x,
                    (corner & 2u) ? chunk->localMax.y : chunk->localMin.y,
                    (corner & 4u) ? chunk->localMax.z : chunk->localMin.z);
        const float y = rtrVec3Add(chunk->position,
                                   rtrQuatRotate(chunk->rotation, local)).y;
        if (y < minY) minY = y;
    }

    if (minY >= -0.995f)
        return (uint32_t)(minY <= -0.985f);

    chunk->position.y += -1.0f - minY;
    if (chunk->velocity.y < 0.0f)
        chunk->velocity.y *= -0.18f;
    chunk->velocity.x *= 0.82f;
    chunk->velocity.z *= 0.82f;
    chunk->angularVelocity = rtrVec3Mul(chunk->angularVelocity, 0.78f);
    return 1u;
}

static void RTR_UNUSED rtrChunkPhysicsTick(void)
{
    if (!rtrChunkPhysicsActive)
        return;

    float dt = rtrCurrentFrameTime - rtrChunkPhysicsLastTime;
    rtrChunkPhysicsLastTime = rtrCurrentFrameTime;
    if (dt <= 0.0f)
        return;
    if (dt > RTR_FRAME_TIME_CLAMP_SECONDS)
        dt = (float)RTR_FRAME_TIME_CLAMP_SECONDS;

    uint32_t moving = 0u;
    for (uint32_t i = 0u; i < rtrChunkCount; i++) {
        RTRChunk *chunk = &rtrChunks[i];
        if (!chunk->active || !chunk->awake) continue;

        chunk->velocity.y -= 5.8f * dt;
        chunk->position = rtrVec3Add(chunk->position, rtrVec3Mul(chunk->velocity, dt));
        chunk->rotation = rtrQuatIntegrate(chunk->rotation,
                                           chunk->angularVelocity,
                                           dt);
        const uint32_t grounded = rtrChunkFloorCollide(chunk);

        chunk->velocity = rtrVec3Mul(chunk->velocity, 0.992f);
        chunk->angularVelocity = rtrVec3Mul(chunk->angularVelocity, 0.988f);

        if (grounded &&
            rtrVec3Dot(chunk->velocity, chunk->velocity) <= 0.0004f &&
            rtrVec3Dot(chunk->angularVelocity, chunk->angularVelocity) <= 0.0004f) {
            chunk->velocity = rtrVec3(0.0f, 0.0f, 0.0f);
            chunk->angularVelocity = rtrVec3(0.0f, 0.0f, 0.0f);
            chunk->awake = 0u;
        } else {
            moving++;
        }
        rtrStoreChunkState(i);
    }

    if (!moving)
        rtrChunkPhysicsActive = 0u;
}

static void rtrCameraBasis(float cameraYaw,
                           float cameraPitch,
                           float cameraRadius,
                           RTRVec3 *ro,
                           RTRVec3 *forward,
                           RTRVec3 *right,
                           RTRVec3 *up)
{
    const float pitch = fminf(fmaxf(cameraPitch, 0.08f), 1.15f);
    const float radius = fmaxf(cameraRadius, 1.0f);
    const float horizontalRadius = cosf(pitch) * radius;
    const RTRVec3 target = rtrVec3(0.0f, -0.12f, 0.0f);

    *ro = rtrVec3Add(target,
                     rtrVec3(sinf(cameraYaw) * horizontalRadius,
                             sinf(pitch) * radius,
                             cosf(cameraYaw) * horizontalRadius));
    *forward = rtrVec3Normalize(rtrVec3Sub(target, *ro));
    *right = rtrVec3Normalize(rtrVec3Cross(*forward, rtrVec3(0.0f, 1.0f, 0.0f)));
    *up = rtrVec3Cross(*right, *forward);
}

static RTRVec3 rtrRayForPixelCPU(float pixelX,
                                 float pixelY,
                                 float cameraYaw,
                                 float cameraPitch,
                                 float cameraRadius,
                                 RTRVec3 *outRo)
{
    RTRVec3 ro;
    RTRVec3 forward;
    RTRVec3 right;
    RTRVec3 up;
    const float invH = 1.0f / fmaxf((float)rtrSwapExtent.height, 1.0f);
    RTRVec3 ray;

    rtrCameraBasis(cameraYaw,
                   cameraPitch,
                   cameraRadius,
                   &ro,
                   &forward,
                   &right,
                   &up);

    ray = rtrVec3Add(rtrVec3Mul(forward, 1.35f),
                     rtrVec3Add(
                         rtrVec3Mul(right, (1.0f - (float)rtrSwapExtent.width) * invH),
                         rtrVec3Mul(up, ((float)rtrSwapExtent.height - 1.0f) * invH)));
    ray = rtrVec3Add(ray, rtrVec3Mul(right, 2.0f * invH * pixelX));
    ray = rtrVec3Add(ray, rtrVec3Mul(up, -2.0f * invH * pixelY));

    if (outRo) *outRo = ro;
    return rtrVec3Normalize(ray);
}

static void rtrQueueGpuImpulse(float x, float y)
{
    rtrMemoryWords[RTR_MEMORY_IMPULSE_PENDING_WORD] = 1u;
    rtrMemoryWords[RTR_MEMORY_IMPULSE_X_WORD] = rtrF32Word(x);
    rtrMemoryWords[RTR_MEMORY_IMPULSE_Y_WORD] = rtrF32Word(y);
    rtrMemoryWords[RTR_MEMORY_IMPULSE_SEED_WORD] =
        rtrFrameIndex * 747796405u + 2891336453u;
}

static void rtrApplyPendingWindowImpulse(void)
{
    float x = 0.0f;
    float y = 0.0f;
    if (!rtrWindowConsumeImpulse(&x, &y)) return;

    rtrQueueGpuImpulse(x, y);
    fprintf(stderr, "impulse: queued GPU shatter %.0f %.0f\n", x, y);
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
        ((VkDeviceSize)RTR_MEMORY_HEADER_WORDS +
         (VkDeviceSize)RTR_SCENE_WORDS +
         (VkDeviceSize)RTR_CHUNK_WORDS) *
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
    rtrMemoryWords[RTR_MEMORY_VERSION_WORD] = 31u;
    rtrMemoryWords[RTR_MEMORY_WIDTH_WORD] = rtrSwapExtent.width;
    rtrMemoryWords[RTR_MEMORY_HEIGHT_WORD] = rtrSwapExtent.height;
    rtrMemoryWords[RTR_MEMORY_CAMERA_YAW_WORD] = rtrF32Word(RTR_CAMERA_DEFAULT_YAW);
    rtrMemoryWords[RTR_MEMORY_CAMERA_PITCH_WORD] = rtrF32Word(RTR_CAMERA_DEFAULT_PITCH);
    rtrMemoryWords[RTR_MEMORY_CAMERA_RADIUS_WORD] = rtrF32Word(RTR_CAMERA_DEFAULT_RADIUS);
    rtrMemoryWords[RTR_MEMORY_CHUNK_COUNT_WORD] = 0u;
    rtrMemoryWords[RTR_MEMORY_DT_WORD] = rtrF32Word(0.0f);
    rtrMemoryWords[RTR_MEMORY_IMPULSE_PENDING_WORD] = 0u;
    rtrScene(rtrMemoryWords);
    fprintf(stderr, "chunks: GPU shatter armed from intact voxel scene\n");

    return 0;
}

static void rtrUpdateMemoryWith(float time,
                                uint32_t autoOrbit,
                                float cameraYaw,
                                float cameraPitch,
                                float cameraRadius)
{
    float dt = time - rtrCurrentFrameTime;
    if (dt < 0.0f)
        dt = 0.0f;
    if (dt > RTR_FRAME_TIME_CLAMP_SECONDS)
        dt = (float)RTR_FRAME_TIME_CLAMP_SECONDS;
    rtrCurrentFrameTime = time;
    rtrMemoryWords[RTR_MEMORY_WIDTH_WORD] = rtrSwapExtent.width;
    rtrMemoryWords[RTR_MEMORY_HEIGHT_WORD] = rtrSwapExtent.height;
    rtrMemoryWords[RTR_MEMORY_FRAME_WORD] = rtrFrameIndex;
    rtrMemoryWords[RTR_MEMORY_DT_WORD] = rtrF32Word(dt);
    rtrMemoryWords[RTR_MEMORY_IMPULSE_PENDING_WORD] = 0u;
    if (!rtrCameraAutoReady) {
        rtrCameraAutoBase = cameraYaw -
            (autoOrbit ? time * RTR_CAMERA_AUTO_SPEED : 0.0f);
        rtrCameraAutoWasEnabled = autoOrbit;
        rtrCameraAutoReady = 1u;
    } else if (autoOrbit && !rtrCameraAutoWasEnabled) {
        rtrCameraAutoBase = cameraYaw - time * RTR_CAMERA_AUTO_SPEED;
    }

    const float activeCameraYaw = autoOrbit ?
        rtrCameraAutoBase + time * RTR_CAMERA_AUTO_SPEED : cameraYaw;
    if (autoOrbit)
        rtrWindowSetCameraYaw(activeCameraYaw);
    rtrCameraAutoWasEnabled = autoOrbit;

    rtrMemoryWords[RTR_MEMORY_CAMERA_YAW_WORD] = rtrF32Word(activeCameraYaw);
    rtrMemoryWords[RTR_MEMORY_CAMERA_PITCH_WORD] = rtrF32Word(cameraPitch);
    rtrMemoryWords[RTR_MEMORY_CAMERA_RADIUS_WORD] = rtrF32Word(cameraRadius);
}

static void rtrUpdateMemory(void)
{
    uint32_t autoOrbit = RTR_CAMERA_AUTO_DEFAULT;
    float cameraYaw = RTR_CAMERA_DEFAULT_YAW;
    float cameraPitch = RTR_CAMERA_DEFAULT_PITCH;
    float cameraRadius = RTR_CAMERA_DEFAULT_RADIUS;

    rtrWindowCamera(&autoOrbit, &cameraYaw, &cameraPitch, &cameraRadius);
    rtrUpdateMemoryWith(rtrFrameSeconds(),
                        autoOrbit, cameraYaw, cameraPitch, cameraRadius);
}

static void rtrCmdBindCompute(VkCommandBuffer commandBuffer,
                              VkDescriptorSet descriptorSet,
                              uint32_t pipeline)
{
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            rtrPipelineLayout,
                            0u,
                            1u,
                            &descriptorSet,
                            0u,
                            NULL);

    vkCmdBindPipeline(commandBuffer,
                      VK_PIPELINE_BIND_POINT_COMPUTE,
                      rtrPipelines[pipeline]);
}

static void rtrCmdMemoryBarrier(VkCommandBuffer commandBuffer,
                                VkAccessFlags srcAccessMask,
                                VkAccessFlags dstAccessMask)
{
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0u,
                         0u, NULL,
                         1u, &(VkBufferMemoryBarrier){
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = srcAccessMask,
        .dstAccessMask = dstAccessMask,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = rtrMemoryBuffer,
        .offset = 0u,
        .size = VK_WHOLE_SIZE,
    }, 0u, NULL);
}

static void rtrCmdDispatchShatter(VkCommandBuffer commandBuffer,
                                  VkDescriptorSet descriptorSet)
{
    const uint32_t cellGroups =
        (RTR_CHUNK_CELL_COUNT + 63u) / 64u;

    rtrCmdBindCompute(commandBuffer, descriptorSet, RTR_PIPELINE_SHATTER);
    vkCmdDispatch(commandBuffer, cellGroups, 1u, 1u);
}

static void rtrCmdDispatchPhysics(VkCommandBuffer commandBuffer,
                                  VkDescriptorSet descriptorSet)
{
    const uint32_t chunkGroups = (RTR_CHUNK_MAX + 63u) / 64u;

    rtrCmdBindCompute(commandBuffer, descriptorSet, RTR_PIPELINE_PHYSICS);
    vkCmdDispatch(commandBuffer, chunkGroups, 1u, 1u);
}

static void rtrCmdDispatchRender(VkCommandBuffer commandBuffer,
                                 VkDescriptorSet descriptorSet)
{
    const uint32_t pixelGroupsX =
        (rtrSwapExtent.width + RTR_TILE_SIZE - 1u) / RTR_TILE_SIZE;
    const uint32_t pixelGroupsY =
        (rtrSwapExtent.height + RTR_TILE_SIZE - 1u) / RTR_TILE_SIZE;

    rtrCmdBindCompute(commandBuffer, descriptorSet, RTR_PIPELINE_RENDER);
    vkCmdDispatch(commandBuffer, pixelGroupsX, pixelGroupsY, 1u);
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
    rtrCameraAutoReady = 0u;
    rtrCameraAutoWasEnabled = 0u;
    rtrCameraAutoBase = RTR_CAMERA_DEFAULT_YAW;
    rtrChunkCount = 0u;
    rtrChunkPhysicsActive = 0u;
    rtrCurrentFrameTime = 0.0f;
    rtrChunkPhysicsLastTime = 0.0f;

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

    const uint32_t *const shaderWords[RTR_PIPELINE_COUNT] = {
        (const uint32_t *)(const void *)shatterCompSpv,
        (const uint32_t *)(const void *)physicsCompSpv,
        (const uint32_t *)(const void *)renderCompSpv,
    };
    const size_t shaderSizes[RTR_PIPELINE_COUNT] = {
        shatterCompSpv_len,
        physicsCompSpv_len,
        renderCompSpv_len,
    };

    for (uint32_t pipeline = 0u; pipeline < RTR_PIPELINE_COUNT; pipeline++) {
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        vkCreateShaderModule(rtrDevice, &(VkShaderModuleCreateInfo){
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = shaderSizes[pipeline],
            .pCode = shaderWords[pipeline],
        }, NULL, &shaderModule);

        vkCreateComputePipelines(rtrDevice, VK_NULL_HANDLE, 1u,
            &(VkComputePipelineCreateInfo){
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = shaderModule,
                .pName = "main",
            },
            .layout = rtrPipelineLayout,
            .basePipelineIndex = -1,
        }, NULL, rtrPipelines + pipeline);

        vkDestroyShaderModule(rtrDevice, shaderModule, NULL);
    }

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
            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = rtrMemoryBuffer,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        }, 0u, NULL);

        rtrCmdDispatchShatter(rtrCommandBuffers[i], rtrDescriptorSets[i]);
        rtrCmdMemoryBarrier(rtrCommandBuffers[i],
                            VK_ACCESS_SHADER_WRITE_BIT,
                            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        rtrCmdDispatchPhysics(rtrCommandBuffers[i], rtrDescriptorSets[i]);
        rtrCmdMemoryBarrier(rtrCommandBuffers[i],
                            VK_ACCESS_SHADER_WRITE_BIT,
                            VK_ACCESS_SHADER_READ_BIT);
        rtrCmdDispatchRender(rtrCommandBuffers[i], rtrDescriptorSets[i]);

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
    rtrApplyPendingWindowImpulse();

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
        .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = rtrMemoryBuffer,
        .offset = 0u,
        .size = VK_WHOLE_SIZE,
    }, 0u, NULL);

    rtrCmdDispatchShatter(commandBuffer, rtrDescriptorSets[0]);
    rtrCmdMemoryBarrier(commandBuffer,
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    rtrCmdDispatchPhysics(commandBuffer, rtrDescriptorSets[0]);
    rtrCmdMemoryBarrier(commandBuffer,
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_SHADER_READ_BIT);
    rtrCmdDispatchRender(commandBuffer, rtrDescriptorSets[0]);

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
    const uint32_t impulseFrame =
        rtrEnvU32("RTR_XPOST_IMPULSE_FRAME",
                  rtrEnvU32("RTR_XPOST_BLAST_FRAME", UINT32_MAX));
    const float impulseX =
        rtrEnvF32("RTR_XPOST_IMPULSE_X",
                  rtrEnvF32("RTR_XPOST_BLAST_X", (float)width * 0.5f));
    const float impulseY =
        rtrEnvF32("RTR_XPOST_IMPULSE_Y",
                  rtrEnvF32("RTR_XPOST_BLAST_Y", (float)height * 0.5f));

    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    for (uint32_t frame = 0u; frame < frames; frame++) {
        rtrUpdateMemoryWith((float)frame / (float)fps,
                            autoOrbit,
                            cameraYaw,
                            cameraPitch,
                            cameraRadius);
        if (frame == impulseFrame) {
            rtrQueueGpuImpulse(impulseX, impulseY);
            fprintf(stderr,
                    "impulse: scripted GPU shatter frame %u %.0f %.0f\n",
                    frame,
                    impulseX,
                    impulseY);
        }
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
