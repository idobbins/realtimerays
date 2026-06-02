#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef RTR_BVH_LEAF_SIZE
#define RTR_BVH_LEAF_SIZE 8u
#endif

#ifndef RTR_BVH_BIN_COUNT
#define RTR_BVH_BIN_COUNT 32u
#endif

#define RTR_SCENE_BVH_INTERNAL_FLAG 0x80000000u
#define RTR_SCENE_PI 3.14159265358979323846f
#define RTR_SCENE_GROUND_Y -1.0f
#define RTR_SCENE_DISK_RADIUS 2.60f
#define RTR_SCENE_MIN_SPHERE_RADIUS 0.038f
#define RTR_SCENE_MAX_SPHERE_RADIUS 0.080f
#define RTR_SCENE_SPHERE_CLEARANCE 0.004f
#define RTR_SCENE_PLACE_ATTEMPTS 100000u
#define RTR_SCENE_BLUE_NOISE_SIZE 64u
#define RTR_SCENE_BLUE_NOISE_TEXELS (RTR_SCENE_BLUE_NOISE_SIZE * RTR_SCENE_BLUE_NOISE_SIZE)
#define RTR_SCENE_BLUE_NOISE_CANDIDATES 64u

enum {
    RTR_SCENE_COUNT_WORD = 8,
    RTR_SCENE_BOUNDS_MIN_WORD = 9,
    RTR_SCENE_BOUNDS_MAX_WORD = 12,
    RTR_SCENE_BVH_NODE_COUNT_WORD = 15,
    RTR_SCENE_GEOM_WORD = 24,
    RTR_SCENE_SPHERE_COUNT = 1000,
    RTR_SCENE_SPHERE_WORDS = 4,
    RTR_SCENE_MAT_WORD = RTR_SCENE_GEOM_WORD + RTR_SCENE_SPHERE_COUNT * RTR_SCENE_SPHERE_WORDS,
    RTR_SCENE_BVH_WORD = RTR_SCENE_MAT_WORD + RTR_SCENE_SPHERE_COUNT * RTR_SCENE_SPHERE_WORDS,
    RTR_SCENE_BVH_NODE_WORDS = 8,
    RTR_SCENE_BVH_MAX_NODES = RTR_SCENE_SPHERE_COUNT * 2 - 1,
    RTR_SCENE_BLUE_NOISE_WORD = RTR_SCENE_BVH_WORD +
        RTR_SCENE_BVH_MAX_NODES * RTR_SCENE_BVH_NODE_WORDS,
};

typedef struct RTRSphere {
    float geom[4];
    float mat[4];
} RTRSphere;

typedef struct RTRBVHNode {
    float boundsMin[3];
    uint32_t leftFirst;
    float boundsMax[3];
    uint32_t countRight;
} RTRBVHNode;

typedef struct RTRSplit {
    uint32_t axis;
    uint32_t bin;
    float cost;
    uint32_t valid;
} RTRSplit;

static uint32_t rtrSortAxis = 0u;

static uint32_t rtrHashU32(uint32_t value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;

    return value;
}

static float rtrHash01(uint32_t value)
{
    return (float)(rtrHashU32(value) & 0x00ffffffu) / 16777215.0f;
}

static int rtrCompareSphereByRadiusDesc(const void *lhs, const void *rhs)
{
    const RTRSphere *a = (const RTRSphere *)lhs;
    const RTRSphere *b = (const RTRSphere *)rhs;

    if (a->geom[3] > b->geom[3]) return -1;
    if (a->geom[3] < b->geom[3]) return 1;
    return 0;
}

static uint32_t rtrOverlapsPlacedSphere(const RTRSphere *spheres,
                                        uint32_t count,
                                        float x,
                                        float z,
                                        float r)
{
    for (uint32_t i = 0u; i < count; i++) {
        const float dx = x - spheres[i].geom[0];
        const float dz = z - spheres[i].geom[2];
        const float minDist = r + spheres[i].geom[3] + RTR_SCENE_SPHERE_CLEARANCE;

        if (dx * dx + dz * dz < minDist * minDist)
            return 1u;
    }

    return 0u;
}

static void rtrPlaceSphere(RTRSphere *spheres, uint32_t index)
{
    const float r = spheres[index].geom[3];
    const float diskRadius = RTR_SCENE_DISK_RADIUS - r;
    float x = 0.0f;
    float z = 0.0f;
    uint32_t placed = 0u;

    for (uint32_t attempt = 0u; attempt < RTR_SCENE_PLACE_ATTEMPTS; attempt++) {
        const uint32_t seed = index * 747796405u + attempt * 2891336453u + 0x9e3779b9u;
        const float radial = sqrtf(rtrHash01(seed ^ 0x85ebca6bu)) * diskRadius;
        const float angle = 2.0f * RTR_SCENE_PI * rtrHash01(seed ^ 0xc2b2ae35u);

        x = cosf(angle) * radial;
        z = sinf(angle) * radial;

        if (!rtrOverlapsPlacedSphere(spheres, index, x, z, r)) {
            placed = 1u;
            break;
        }
    }

    if (!placed) {
        const float angle = 2.0f * RTR_SCENE_PI * rtrHash01(index * 2246822519u);
        x = cosf(angle) * diskRadius;
        z = sinf(angle) * diskRadius;
    }

    spheres[index].geom[0] = x;
    spheres[index].geom[1] = RTR_SCENE_GROUND_Y + r;
    spheres[index].geom[2] = z;
}

static uint32_t rtrToroidalDistance2(uint32_t a, uint32_t b)
{
    const uint32_t ax = a & (RTR_SCENE_BLUE_NOISE_SIZE - 1u);
    const uint32_t ay = a >> 6u;
    const uint32_t bx = b & (RTR_SCENE_BLUE_NOISE_SIZE - 1u);
    const uint32_t by = b >> 6u;
    uint32_t dx = ax > bx ? ax - bx : bx - ax;
    uint32_t dy = ay > by ? ay - by : by - ay;

    if (dx > RTR_SCENE_BLUE_NOISE_SIZE / 2u) dx = RTR_SCENE_BLUE_NOISE_SIZE - dx;
    if (dy > RTR_SCENE_BLUE_NOISE_SIZE / 2u) dy = RTR_SCENE_BLUE_NOISE_SIZE - dy;

    return dx * dx + dy * dy;
}

static void rtrUpdateBlueNoiseDistances(float *distances, uint32_t chosen)
{
    for (uint32_t i = 0u; i < RTR_SCENE_BLUE_NOISE_TEXELS; i++) {
        const float dist = (float)rtrToroidalDistance2(i, chosen);
        if (dist < distances[i]) distances[i] = dist;
    }
}

static void rtrGenerateBlueNoiseChannel(uint16_t *values, uint32_t seed)
{
    float distances[RTR_SCENE_BLUE_NOISE_TEXELS];
    uint8_t used[RTR_SCENE_BLUE_NOISE_TEXELS];

    for (uint32_t i = 0u; i < RTR_SCENE_BLUE_NOISE_TEXELS; i++) {
        distances[i] = 1.0e20f;
        used[i] = 0u;
        values[i] = 0u;
    }

    for (uint32_t rank = 0u; rank < RTR_SCENE_BLUE_NOISE_TEXELS; rank++) {
        uint32_t best = 0xffffffffu;
        float bestScore = -1.0f;

        for (uint32_t candidate = 0u;
             candidate < RTR_SCENE_BLUE_NOISE_CANDIDATES;
             candidate++) {
            const uint32_t index =
                rtrHashU32(seed ^ rank * 0x9e3779b9u ^ candidate * 0x85ebca6bu) &
                (RTR_SCENE_BLUE_NOISE_TEXELS - 1u);
            if (used[index]) continue;

            const float jitter =
                rtrHash01(seed ^ rank * 0xc2b2ae35u ^ candidate * 0x27d4eb2du);
            const float score = distances[index] + jitter * 0.001f;
            if (score > bestScore) {
                bestScore = score;
                best = index;
            }
        }

        if (best == 0xffffffffu) {
            for (uint32_t i = 0u; i < RTR_SCENE_BLUE_NOISE_TEXELS; i++) {
                if (used[i]) continue;
                if (distances[i] > bestScore) {
                    bestScore = distances[i];
                    best = i;
                }
            }
        }

        used[best] = 1u;
        values[best] = (uint16_t)((rank * 65535u) / (RTR_SCENE_BLUE_NOISE_TEXELS - 1u));
        rtrUpdateBlueNoiseDistances(distances, best);
    }
}

static void rtrStoreBlueNoise(uint32_t *words)
{
    uint16_t channel0[RTR_SCENE_BLUE_NOISE_TEXELS];
    uint16_t channel1[RTR_SCENE_BLUE_NOISE_TEXELS];

    rtrGenerateBlueNoiseChannel(channel0, 0x13a5f00du);
    rtrGenerateBlueNoiseChannel(channel1, 0xbad5eedu);

    for (uint32_t i = 0u; i < RTR_SCENE_BLUE_NOISE_TEXELS; i++) {
        words[RTR_SCENE_BLUE_NOISE_WORD + i] =
            (uint32_t)channel0[i] | ((uint32_t)channel1[i] << 16u);
    }
}

static void rtrBoundsReset(float boundsMin[3], float boundsMax[3])
{
    boundsMin[0] = boundsMin[1] = boundsMin[2] = 1.0e20f;
    boundsMax[0] = boundsMax[1] = boundsMax[2] = -1.0e20f;
}

static void rtrBoundsExtend(float boundsMin[3], float boundsMax[3],
                            const float otherMin[3], const float otherMax[3])
{
    for (uint32_t axis = 0u; axis < 3u; axis++) {
        if (otherMin[axis] < boundsMin[axis]) boundsMin[axis] = otherMin[axis];
        if (otherMax[axis] > boundsMax[axis]) boundsMax[axis] = otherMax[axis];
    }
}

static void rtrSphereBounds(const RTRSphere *sphere, float boundsMin[3], float boundsMax[3])
{
    const float r = sphere->geom[3];
    boundsMin[0] = sphere->geom[0] - r;
    boundsMin[1] = sphere->geom[1] - r;
    boundsMin[2] = sphere->geom[2] - r;
    boundsMax[0] = sphere->geom[0] + r;
    boundsMax[1] = sphere->geom[1] + r;
    boundsMax[2] = sphere->geom[2] + r;
}

static float rtrBoundsArea(const float boundsMin[3], const float boundsMax[3])
{
    const float dx = boundsMax[0] > boundsMin[0] ? boundsMax[0] - boundsMin[0] : 0.0f;
    const float dy = boundsMax[1] > boundsMin[1] ? boundsMax[1] - boundsMin[1] : 0.0f;
    const float dz = boundsMax[2] > boundsMin[2] ? boundsMax[2] - boundsMin[2] : 0.0f;

    return 2.0f * (dx * dy + dx * dz + dy * dz);
}

static int rtrCompareSphereByAxis(const void *lhs, const void *rhs)
{
    const RTRSphere *a = (const RTRSphere *)lhs;
    const RTRSphere *b = (const RTRSphere *)rhs;

    if (a->geom[rtrSortAxis] < b->geom[rtrSortAxis]) return -1;
    if (a->geom[rtrSortAxis] > b->geom[rtrSortAxis]) return 1;
    return 0;
}

static uint32_t rtrLongestAxis(const float boundsMin[3], const float boundsMax[3])
{
    const float ex = boundsMax[0] - boundsMin[0];
    const float ey = boundsMax[1] - boundsMin[1];
    const float ez = boundsMax[2] - boundsMin[2];

    if (ex >= ey && ex >= ez) return 0u;
    return ey >= ez ? 1u : 2u;
}

static void rtrCentroidBounds(const RTRSphere *spheres, uint32_t start, uint32_t count,
                              float boundsMin[3], float boundsMax[3])
{
    rtrBoundsReset(boundsMin, boundsMax);

    for (uint32_t i = 0u; i < count; i++) {
        const RTRSphere *sphere = spheres + start + i;
        for (uint32_t axis = 0u; axis < 3u; axis++) {
            const float value = sphere->geom[axis];
            if (value < boundsMin[axis]) boundsMin[axis] = value;
            if (value > boundsMax[axis]) boundsMax[axis] = value;
        }
    }
}

static void rtrNodeBounds(const RTRSphere *spheres, uint32_t start, uint32_t count,
                          float boundsMin[3], float boundsMax[3])
{
    rtrBoundsReset(boundsMin, boundsMax);

    for (uint32_t i = 0u; i < count; i++) {
        float sphereMin[3];
        float sphereMax[3];
        rtrSphereBounds(spheres + start + i, sphereMin, sphereMax);
        rtrBoundsExtend(boundsMin, boundsMax, sphereMin, sphereMax);
    }
}

static RTRSplit rtrFindSplit(const RTRSphere *spheres, uint32_t start, uint32_t count)
{
    RTRSplit best = {.cost = 1.0e30f};
    float centroidMin[3];
    float centroidMax[3];

    rtrCentroidBounds(spheres, start, count, centroidMin, centroidMax);

    for (uint32_t axis = 0u; axis < 3u; axis++) {
        const float extent = centroidMax[axis] - centroidMin[axis];
        if (extent <= 1.0e-8f) continue;

        uint32_t binCounts[RTR_BVH_BIN_COUNT];
        float binMin[RTR_BVH_BIN_COUNT][3];
        float binMax[RTR_BVH_BIN_COUNT][3];
        for (uint32_t bin = 0u; bin < RTR_BVH_BIN_COUNT; bin++) {
            binCounts[bin] = 0u;
            rtrBoundsReset(binMin[bin], binMax[bin]);
        }

        const float scale = ((float)RTR_BVH_BIN_COUNT - 1.0f) / extent;
        for (uint32_t i = 0u; i < count; i++) {
            const RTRSphere *sphere = spheres + start + i;
            uint32_t bin = (uint32_t)((sphere->geom[axis] - centroidMin[axis]) * scale);
            if (bin >= RTR_BVH_BIN_COUNT) bin = RTR_BVH_BIN_COUNT - 1u;

            float sphereMin[3];
            float sphereMax[3];
            rtrSphereBounds(sphere, sphereMin, sphereMax);
            binCounts[bin]++;
            rtrBoundsExtend(binMin[bin], binMax[bin], sphereMin, sphereMax);
        }

        for (uint32_t splitBin = 0u; splitBin + 1u < RTR_BVH_BIN_COUNT; splitBin++) {
            uint32_t leftCount = 0u;
            uint32_t rightCount = 0u;
            float leftMin[3];
            float leftMax[3];
            float rightMin[3];
            float rightMax[3];

            rtrBoundsReset(leftMin, leftMax);
            rtrBoundsReset(rightMin, rightMax);

            for (uint32_t bin = 0u; bin <= splitBin; bin++) {
                if (!binCounts[bin]) continue;
                leftCount += binCounts[bin];
                rtrBoundsExtend(leftMin, leftMax, binMin[bin], binMax[bin]);
            }

            for (uint32_t bin = splitBin + 1u; bin < RTR_BVH_BIN_COUNT; bin++) {
                if (!binCounts[bin]) continue;
                rightCount += binCounts[bin];
                rtrBoundsExtend(rightMin, rightMax, binMin[bin], binMax[bin]);
            }

            if (!leftCount || !rightCount) continue;

            const float cost =
                rtrBoundsArea(leftMin, leftMax) * (float)leftCount +
                rtrBoundsArea(rightMin, rightMax) * (float)rightCount;
            if (cost < best.cost) {
                best = (RTRSplit){
                    .axis = axis,
                    .bin = splitBin,
                    .cost = cost,
                    .valid = 1u,
                };
            }
        }
    }

    return best;
}

static uint32_t rtrPartitionSpheres(RTRSphere *spheres, uint32_t start, uint32_t count,
                                    RTRSplit split)
{
    float centroidMin[3];
    float centroidMax[3];
    rtrCentroidBounds(spheres, start, count, centroidMin, centroidMax);

    const float extent = centroidMax[split.axis] - centroidMin[split.axis];
    if (extent <= 1.0e-8f) return start;

    uint32_t left = start;
    uint32_t right = start + count;
    const float scale = ((float)RTR_BVH_BIN_COUNT - 1.0f) / extent;

    while (left < right) {
        uint32_t bin = (uint32_t)((spheres[left].geom[split.axis] - centroidMin[split.axis]) *
                                  scale);
        if (bin >= RTR_BVH_BIN_COUNT) bin = RTR_BVH_BIN_COUNT - 1u;

        if (bin <= split.bin) {
            left++;
        } else {
            right--;
            RTRSphere tmp = spheres[left];
            spheres[left] = spheres[right];
            spheres[right] = tmp;
        }
    }

    return left;
}

static uint32_t rtrBuildBVH(RTRSphere *spheres, RTRBVHNode *nodes, uint32_t *nodeCount,
                            uint32_t start, uint32_t count)
{
    const uint32_t nodeIndex = (*nodeCount)++;
    RTRBVHNode *node = nodes + nodeIndex;

    rtrNodeBounds(spheres, start, count, node->boundsMin, node->boundsMax);

    if (count <= RTR_BVH_LEAF_SIZE) {
        node->leftFirst = start;
        node->countRight = count;
        return nodeIndex;
    }

    RTRSplit split = rtrFindSplit(spheres, start, count);
    uint32_t mid = split.valid ? rtrPartitionSpheres(spheres, start, count, split) : start;

    if (mid == start || mid == start + count) {
        rtrSortAxis = rtrLongestAxis(node->boundsMin, node->boundsMax);
        qsort(spheres + start, count, sizeof(spheres[0]), rtrCompareSphereByAxis);
        mid = start + count / 2u;
    }

    const uint32_t left = rtrBuildBVH(spheres, nodes, nodeCount, start, mid - start);
    const uint32_t right = rtrBuildBVH(spheres, nodes, nodeCount, mid, start + count - mid);

    node->leftFirst = left;
    node->countRight = RTR_SCENE_BVH_INTERNAL_FLAG | right;
    return nodeIndex;
}

static void rtrStoreBVHNode(uint32_t *words, uint32_t index, const RTRBVHNode *node)
{
    const uint32_t word = RTR_SCENE_BVH_WORD + index * RTR_SCENE_BVH_NODE_WORDS;

    memcpy(words + word, node->boundsMin, sizeof(node->boundsMin));
    words[word + 3u] = node->leftFirst;
    memcpy(words + word + 4u, node->boundsMax, sizeof(node->boundsMax));
    words[word + 7u] = node->countRight;
}

void rtrScene(uint32_t *words)
{
    words[RTR_SCENE_COUNT_WORD] = RTR_SCENE_SPHERE_COUNT;

    RTRSphere spheres[RTR_SCENE_SPHERE_COUNT];
    RTRBVHNode nodes[RTR_SCENE_BVH_MAX_NODES];
    uint32_t nodeCount = 0u;

    for (uint32_t i = 0u; i < RTR_SCENE_SPHERE_COUNT; i++) {
        const float h = rtrHash01(i * 747796405u + 0x165667b1u);
        const float r =
            RTR_SCENE_MIN_SPHERE_RADIUS +
            (RTR_SCENE_MAX_SPHERE_RADIUS - RTR_SCENE_MIN_SPHERE_RADIUS) * h;

        spheres[i] = (RTRSphere){
            .geom = {0.0f, RTR_SCENE_GROUND_Y + r, 0.0f, r},
        };
    }

    qsort(spheres, RTR_SCENE_SPHERE_COUNT, sizeof(spheres[0]), rtrCompareSphereByRadiusDesc);

    for (uint32_t i = 0u; i < RTR_SCENE_SPHERE_COUNT; i++) {
        rtrPlaceSphere(spheres, i);

        const float u = spheres[i].geom[0] / (RTR_SCENE_DISK_RADIUS * 2.0f) + 0.5f;
        const float v = spheres[i].geom[2] / (RTR_SCENE_DISK_RADIUS * 2.0f) + 0.5f;
        const float w = rtrHash01(i * 2246822519u + 3266489917u);
        const float r = spheres[i].geom[3];

        spheres[i].mat[0] = 0.24f + 0.52f * u;
        spheres[i].mat[1] = 0.24f + 0.42f * w;
        spheres[i].mat[2] = 0.34f + 0.36f * v;
        spheres[i].mat[3] = 1.0f / r;
    }

    rtrBuildBVH(spheres, nodes, &nodeCount, 0u, RTR_SCENE_SPHERE_COUNT);

    memcpy(words + RTR_SCENE_BOUNDS_MIN_WORD, nodes[0].boundsMin, sizeof(nodes[0].boundsMin));
    memcpy(words + RTR_SCENE_BOUNDS_MAX_WORD, nodes[0].boundsMax, sizeof(nodes[0].boundsMax));
    words[RTR_SCENE_BVH_NODE_COUNT_WORD] = nodeCount;

    for (uint32_t i = 0u; i < RTR_SCENE_SPHERE_COUNT; i++) {
        memcpy(words + RTR_SCENE_GEOM_WORD + i * RTR_SCENE_SPHERE_WORDS,
               spheres[i].geom, sizeof(spheres[i].geom));
        memcpy(words + RTR_SCENE_MAT_WORD + i * RTR_SCENE_SPHERE_WORDS,
               spheres[i].mat, sizeof(spheres[i].mat));
    }

    for (uint32_t i = 0u; i < nodeCount; i++) {
        rtrStoreBVHNode(words, i, nodes + i);
    }

    rtrStoreBlueNoise(words);
}
