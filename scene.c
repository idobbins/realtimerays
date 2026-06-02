#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#ifndef RTR_BVH_LEAF_SIZE
#define RTR_BVH_LEAF_SIZE 8u
#endif

#ifndef RTR_BVH_BIN_COUNT
#define RTR_BVH_BIN_COUNT 32u
#endif

#define RTR_SCENE_BVH_INTERNAL_FLAG 0x80000000u

enum {
    RTR_SCENE_COUNT_WORD = 8,
    RTR_SCENE_BOUNDS_MIN_WORD = 9,
    RTR_SCENE_BOUNDS_MAX_WORD = 12,
    RTR_SCENE_BVH_NODE_COUNT_WORD = 15,
    RTR_SCENE_GEOM_WORD = 16,
    RTR_SCENE_SPHERE_COUNT = 1000,
    RTR_SCENE_GRID_X = 40,
    RTR_SCENE_GRID_Z = 25,
    RTR_SCENE_SPHERE_WORDS = 4,
    RTR_SCENE_MAT_WORD = RTR_SCENE_GEOM_WORD + RTR_SCENE_SPHERE_COUNT * RTR_SCENE_SPHERE_WORDS,
    RTR_SCENE_BVH_WORD = RTR_SCENE_MAT_WORD + RTR_SCENE_SPHERE_COUNT * RTR_SCENE_SPHERE_WORDS,
    RTR_SCENE_BVH_NODE_WORDS = 8,
    RTR_SCENE_BVH_MAX_NODES = RTR_SCENE_SPHERE_COUNT * 2 - 1,
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

    for (uint32_t z = 0u; z < RTR_SCENE_GRID_Z; z++) {
        for (uint32_t x = 0u; x < RTR_SCENE_GRID_X; x++) {
            const uint32_t i = z * RTR_SCENE_GRID_X + x;
            const float fx = ((float)x - ((float)RTR_SCENE_GRID_X - 1.0f) * 0.5f) * 0.105f;
            const float fz = ((float)z - ((float)RTR_SCENE_GRID_Z - 1.0f) * 0.5f) * 0.16f;
            const float r = 0.025f + (float)((x * 3u + z * 5u) % 5u) * 0.006f;
            const float u = (float)x / ((float)RTR_SCENE_GRID_X - 1.0f);
            const float v = (float)z / ((float)RTR_SCENE_GRID_Z - 1.0f);
            const float w = (float)((x + z) % 10u) / 9.0f;

            spheres[i] = (RTRSphere){
                .geom = {fx, -1.0f + r, fz, r},
                .mat = {
                    0.24f + 0.56f * u,
                    0.25f + 0.38f * w,
                    0.34f + 0.42f * v,
                    1.0f / r,
                },
            };
        }
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
}
