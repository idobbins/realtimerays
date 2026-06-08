#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef RTR_BVH_LEAF_SIZE
#define RTR_BVH_LEAF_SIZE 2u
#endif

#ifndef RTR_BVH_BIN_COUNT
#define RTR_BVH_BIN_COUNT 8u
#endif

#ifndef RTR_SCENE_MESH_PATH
#define RTR_SCENE_MESH_PATH "assets/castles/castle6.ply"
#endif

#ifndef RTR_SCENE_HDRI_PATH
#define RTR_SCENE_HDRI_PATH "assets/hdri/spaichingen_hill_1024x512_rgba16f.bin"
#endif

#ifndef RTR_SCENE_HDRI_DIFFUSE_PATH
#define RTR_SCENE_HDRI_DIFFUSE_PATH "assets/hdri/spaichingen_hill_diffuse_32x16_rgba16f.bin"
#endif

#define RTR_SCENE_BVH_INTERNAL_FLAG 0x80000000u
#define RTR_SCENE_PLANE_Y -1.0f
#define RTR_SCENE_MESH_HEIGHT 1.75f
#define RTR_SCENE_BOUNDS_EPS 0.0001f
#define RTR_SCENE_BLUE_NOISE_SIZE 64u
#define RTR_SCENE_BLUE_NOISE_TEXELS (RTR_SCENE_BLUE_NOISE_SIZE * RTR_SCENE_BLUE_NOISE_SIZE)
#define RTR_SCENE_BLUE_NOISE_CANDIDATES 64u
#define RTR_SCENE_TRIANGLE_QUANT_MAX 65535u
#define RTR_SCENE_ENVMAP_WIDTH 1024u
#define RTR_SCENE_ENVMAP_HEIGHT 512u
#define RTR_SCENE_ENVMAP_WORDS (RTR_SCENE_ENVMAP_WIDTH * RTR_SCENE_ENVMAP_HEIGHT * 2u)
#define RTR_SCENE_ENVMAP_DIFFUSE_WIDTH 32u
#define RTR_SCENE_ENVMAP_DIFFUSE_HEIGHT 16u
#define RTR_SCENE_ENVMAP_DIFFUSE_WORDS \
    (RTR_SCENE_ENVMAP_DIFFUSE_WIDTH * RTR_SCENE_ENVMAP_DIFFUSE_HEIGHT * 2u)

enum {
    RTR_SCENE_COUNT_WORD = 8,
    RTR_SCENE_BVH_NODE_COUNT_WORD = 15,
    RTR_SCENE_QUANT_MIN_WORD = 24,
    RTR_SCENE_QUANT_MAX_WORD = 27,
    RTR_SCENE_GEOM_WORD = 32,
    RTR_SCENE_TRIANGLE_CAPACITY = 900000,
    RTR_SCENE_TRIANGLE_WORDS = 5,
    RTR_SCENE_BVH_WORD = RTR_SCENE_GEOM_WORD +
        RTR_SCENE_TRIANGLE_CAPACITY * RTR_SCENE_TRIANGLE_WORDS,
    RTR_SCENE_BVH_NODE_WORDS = 8,
    RTR_SCENE_BVH_MAX_NODES = RTR_SCENE_TRIANGLE_CAPACITY * 2 - 1,
    RTR_SCENE_BLUE_NOISE_WORD = RTR_SCENE_BVH_WORD +
        RTR_SCENE_BVH_MAX_NODES * RTR_SCENE_BVH_NODE_WORDS,
    RTR_SCENE_ENVMAP_WORD = RTR_SCENE_BLUE_NOISE_WORD +
        RTR_SCENE_BLUE_NOISE_TEXELS,
    RTR_SCENE_ENVMAP_DIFFUSE_WORD = RTR_SCENE_ENVMAP_WORD +
        RTR_SCENE_ENVMAP_WORDS,
};

typedef struct RTRVec3 {
    float x;
    float y;
    float z;
} RTRVec3;

typedef struct RTRTriangle {
    RTRVec3 v0;
    RTRVec3 v1;
    RTRVec3 v2;
    RTRVec3 centroid;
} RTRTriangle;

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

static uint32_t rtrF32Word(float value)
{
    uint32_t word = 0u;
    memcpy(&word, &value, sizeof(word));
    return word;
}

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

static uint32_t rtrPackU16(uint32_t lo, uint32_t hi)
{
    return (lo & 0xffffu) | ((hi & 0xffffu) << 16u);
}

static uint32_t rtrQuantizeUnormNearest(float value,
                                        float minValue,
                                        float maxValue,
                                        uint32_t maxQuant)
{
    const float extent = maxValue - minValue;
    float q;

    if (!(extent > 0.0f))
        return 0u;

    q = (value - minValue) * (float)maxQuant / extent;
    if (q <= 0.0f) return 0u;
    if (q >= (float)maxQuant) return maxQuant;
    return (uint32_t)floorf(q + 0.5f);
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

static uint32_t rtrF32ToF16Word(float value)
{
    uint32_t bits = 0u;
    uint32_t sign;
    int32_t exponent;
    uint32_t mantissa;

    if (!(value > 0.0f))
        return 0u;
    if (value > 65504.0f)
        value = 65504.0f;

    memcpy(&bits, &value, sizeof(bits));
    sign = (bits >> 16u) & 0x8000u;
    exponent = (int32_t)((bits >> 23u) & 0xffu) - 127 + 15;
    mantissa = bits & 0x7fffffu;

    if (exponent <= 0)
        return sign;
    if (exponent >= 31)
        return sign | 0x7bffu;

    return sign | ((uint32_t)exponent << 10u) | (mantissa >> 13u);
}

static uint32_t rtrPackF16x2(float lo, float hi)
{
    return rtrF32ToF16Word(lo) | (rtrF32ToF16Word(hi) << 16u);
}

static void rtrStoreFallbackEnvironment(uint32_t *words,
                                        uint32_t startWord,
                                        uint32_t width,
                                        uint32_t height)
{
    for (uint32_t y = 0u; y < height; y++) {
        const float v = ((float)y + 0.5f) / (float)height;
        const float rdY = cosf(v * 3.14159265f);
        const float h = fmaxf(0.0f, fminf(1.0f, rdY * 0.5f + 0.5f));
        const float r = 0.58f * (1.0f - h) + 0.10f * h;
        const float g = 0.62f * (1.0f - h) + 0.15f * h;
        const float b = 0.68f * (1.0f - h) + 0.22f * h;

        for (uint32_t x = 0u; x < width; x++) {
            const uint32_t word = startWord + (y * width + x) * 2u;
            words[word] = rtrPackF16x2(r, g);
            words[word + 1u] = rtrPackF16x2(b, 1.0f);
        }
    }
}

static void rtrStoreEnvironmentFile(uint32_t *words,
                                    const char *path,
                                    uint32_t startWord,
                                    uint32_t width,
                                    uint32_t height,
                                    uint32_t wordCount)
{
    FILE *file = fopen(path, "rb");
    size_t readCount = 0u;

    if (!file) {
        fprintf(stderr, "scene: failed to open %s, using fallback sky\n",
                path);
        rtrStoreFallbackEnvironment(words, startWord, width, height);
        return;
    }

    readCount = fread(words + startWord,
                      sizeof(uint32_t),
                      wordCount,
                      file);
    fclose(file);

    if (readCount != wordCount) {
        fprintf(stderr, "scene: failed to read %s, using fallback sky\n",
                path);
        rtrStoreFallbackEnvironment(words, startWord, width, height);
    }
}

static void rtrStoreEnvironment(uint32_t *words)
{
    rtrStoreEnvironmentFile(words,
                            RTR_SCENE_HDRI_PATH,
                            RTR_SCENE_ENVMAP_WORD,
                            RTR_SCENE_ENVMAP_WIDTH,
                            RTR_SCENE_ENVMAP_HEIGHT,
                            RTR_SCENE_ENVMAP_WORDS);
    rtrStoreEnvironmentFile(words,
                            RTR_SCENE_HDRI_DIFFUSE_PATH,
                            RTR_SCENE_ENVMAP_DIFFUSE_WORD,
                            RTR_SCENE_ENVMAP_DIFFUSE_WIDTH,
                            RTR_SCENE_ENVMAP_DIFFUSE_HEIGHT,
                            RTR_SCENE_ENVMAP_DIFFUSE_WORDS);
}

static void rtrBoundsReset(float boundsMin[3], float boundsMax[3])
{
    boundsMin[0] = boundsMin[1] = boundsMin[2] = 1.0e20f;
    boundsMax[0] = boundsMax[1] = boundsMax[2] = -1.0e20f;
}

static void rtrBoundsExtendPoint(float boundsMin[3], float boundsMax[3], RTRVec3 p)
{
    if (p.x < boundsMin[0]) boundsMin[0] = p.x;
    if (p.y < boundsMin[1]) boundsMin[1] = p.y;
    if (p.z < boundsMin[2]) boundsMin[2] = p.z;
    if (p.x > boundsMax[0]) boundsMax[0] = p.x;
    if (p.y > boundsMax[1]) boundsMax[1] = p.y;
    if (p.z > boundsMax[2]) boundsMax[2] = p.z;
}

static void rtrBoundsExtend(float boundsMin[3], float boundsMax[3],
                            const float otherMin[3], const float otherMax[3])
{
    for (uint32_t axis = 0u; axis < 3u; axis++) {
        if (otherMin[axis] < boundsMin[axis]) boundsMin[axis] = otherMin[axis];
        if (otherMax[axis] > boundsMax[axis]) boundsMax[axis] = otherMax[axis];
    }
}

static void rtrTriangleFinalize(RTRTriangle *triangle)
{
    triangle->centroid.x = (triangle->v0.x + triangle->v1.x + triangle->v2.x) / 3.0f;
    triangle->centroid.y = (triangle->v0.y + triangle->v1.y + triangle->v2.y) / 3.0f;
    triangle->centroid.z = (triangle->v0.z + triangle->v1.z + triangle->v2.z) / 3.0f;
}

static void rtrTriangleBounds(const RTRTriangle *triangle,
                              float boundsMin[3],
                              float boundsMax[3])
{
    rtrBoundsReset(boundsMin, boundsMax);
    rtrBoundsExtendPoint(boundsMin, boundsMax, triangle->v0);
    rtrBoundsExtendPoint(boundsMin, boundsMax, triangle->v1);
    rtrBoundsExtendPoint(boundsMin, boundsMax, triangle->v2);

    for (uint32_t axis = 0u; axis < 3u; axis++) {
        boundsMin[axis] -= RTR_SCENE_BOUNDS_EPS;
        boundsMax[axis] += RTR_SCENE_BOUNDS_EPS;
    }
}

static float rtrBoundsArea(const float boundsMin[3], const float boundsMax[3])
{
    const float dx = boundsMax[0] > boundsMin[0] ? boundsMax[0] - boundsMin[0] : 0.0f;
    const float dy = boundsMax[1] > boundsMin[1] ? boundsMax[1] - boundsMin[1] : 0.0f;
    const float dz = boundsMax[2] > boundsMin[2] ? boundsMax[2] - boundsMin[2] : 0.0f;

    return 2.0f * (dx * dy + dx * dz + dy * dz);
}

static float rtrTriangleCentroidAxis(const RTRTriangle *triangle, uint32_t axis)
{
    if (axis == 0u) return triangle->centroid.x;
    if (axis == 1u) return triangle->centroid.y;
    return triangle->centroid.z;
}

static int rtrCompareTriangleByAxis(const void *lhs, const void *rhs)
{
    const RTRTriangle *a = (const RTRTriangle *)lhs;
    const RTRTriangle *b = (const RTRTriangle *)rhs;
    const float av = rtrTriangleCentroidAxis(a, rtrSortAxis);
    const float bv = rtrTriangleCentroidAxis(b, rtrSortAxis);

    if (av < bv) return -1;
    if (av > bv) return 1;
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

static void rtrCentroidBounds(const RTRTriangle *triangles,
                              uint32_t start,
                              uint32_t count,
                              float boundsMin[3],
                              float boundsMax[3])
{
    rtrBoundsReset(boundsMin, boundsMax);

    for (uint32_t i = 0u; i < count; i++) {
        const RTRTriangle *triangle = triangles + start + i;
        rtrBoundsExtendPoint(boundsMin, boundsMax, triangle->centroid);
    }
}

static void rtrNodeBounds(const RTRTriangle *triangles,
                          uint32_t start,
                          uint32_t count,
                          float boundsMin[3],
                          float boundsMax[3])
{
    rtrBoundsReset(boundsMin, boundsMax);

    for (uint32_t i = 0u; i < count; i++) {
        float triMin[3];
        float triMax[3];
        rtrTriangleBounds(triangles + start + i, triMin, triMax);
        rtrBoundsExtend(boundsMin, boundsMax, triMin, triMax);
    }
}

static RTRSplit rtrFindSplit(const RTRTriangle *triangles, uint32_t start, uint32_t count)
{
    RTRSplit best = {.cost = 1.0e30f};
    float centroidMin[3];
    float centroidMax[3];

    rtrCentroidBounds(triangles, start, count, centroidMin, centroidMax);

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
            const RTRTriangle *triangle = triangles + start + i;
            uint32_t bin = (uint32_t)(
                (rtrTriangleCentroidAxis(triangle, axis) - centroidMin[axis]) * scale);
            float triMin[3];
            float triMax[3];

            if (bin >= RTR_BVH_BIN_COUNT) bin = RTR_BVH_BIN_COUNT - 1u;

            rtrTriangleBounds(triangle, triMin, triMax);
            binCounts[bin]++;
            rtrBoundsExtend(binMin[bin], binMax[bin], triMin, triMax);
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

static uint32_t rtrPartitionTriangles(RTRTriangle *triangles,
                                      uint32_t start,
                                      uint32_t count,
                                      RTRSplit split)
{
    float centroidMin[3];
    float centroidMax[3];
    rtrCentroidBounds(triangles, start, count, centroidMin, centroidMax);

    const float extent = centroidMax[split.axis] - centroidMin[split.axis];
    if (extent <= 1.0e-8f) return start;

    uint32_t left = start;
    uint32_t right = start + count;
    const float scale = ((float)RTR_BVH_BIN_COUNT - 1.0f) / extent;

    while (left < right) {
        uint32_t bin = (uint32_t)(
            (rtrTriangleCentroidAxis(triangles + left, split.axis) -
             centroidMin[split.axis]) * scale);
        if (bin >= RTR_BVH_BIN_COUNT) bin = RTR_BVH_BIN_COUNT - 1u;

        if (bin <= split.bin) {
            left++;
        } else {
            right--;
            RTRTriangle tmp = triangles[left];
            triangles[left] = triangles[right];
            triangles[right] = tmp;
        }
    }

    return left;
}

static uint32_t rtrBuildBVH(RTRTriangle *triangles,
                            RTRBVHNode *nodes,
                            uint32_t *nodeCount,
                            uint32_t start,
                            uint32_t count)
{
    const uint32_t nodeIndex = (*nodeCount)++;
    RTRBVHNode *node = nodes + nodeIndex;

    rtrNodeBounds(triangles, start, count, node->boundsMin, node->boundsMax);

    if (count <= RTR_BVH_LEAF_SIZE) {
        node->leftFirst = start;
        node->countRight = count;
        return nodeIndex;
    }

    RTRSplit split = rtrFindSplit(triangles, start, count);
    uint32_t mid = split.valid ?
        rtrPartitionTriangles(triangles, start, count, split) :
        start;

    if (mid == start || mid == start + count) {
        rtrSortAxis = rtrLongestAxis(node->boundsMin, node->boundsMax);
        qsort(triangles + start, count, sizeof(triangles[0]), rtrCompareTriangleByAxis);
        mid = start + count / 2u;
    }

    const uint32_t left = rtrBuildBVH(triangles, nodes, nodeCount, start, mid - start);
    const uint32_t right = rtrBuildBVH(triangles, nodes, nodeCount, mid, start + count - mid);

    node->leftFirst = left;
    node->countRight = RTR_SCENE_BVH_INTERNAL_FLAG | right;
    return nodeIndex;
}

static int rtrReadLine(FILE *file, char *line, size_t lineSize)
{
    return fgets(line, (int)lineSize, file) != NULL;
}

static int rtrParsePlyHeader(FILE *file, uint32_t *vertexCount, uint32_t *faceCount)
{
    char line[256];

    *vertexCount = 0u;
    *faceCount = 0u;

    if (!rtrReadLine(file, line, sizeof(line)) || strcmp(line, "ply\n") != 0)
        return 1;

    while (rtrReadLine(file, line, sizeof(line))) {
        unsigned parsedCount = 0u;

        if (sscanf(line, "element vertex %u", &parsedCount) == 1) {
            *vertexCount = (uint32_t)parsedCount;
            continue;
        }

        if (sscanf(line, "element face %u", &parsedCount) == 1) {
            *faceCount = (uint32_t)parsedCount;
            continue;
        }

        if (strcmp(line, "end_header\n") == 0)
            return (*vertexCount == 0u || *faceCount == 0u) ? 1 : 0;
    }

    return 1;
}

static int rtrLoadMeshTriangles(RTRTriangle **outTriangles, uint32_t *outTriangleCount)
{
    FILE *file = fopen(RTR_SCENE_MESH_PATH, "r");
    uint32_t vertexCount = 0u;
    uint32_t faceCount = 0u;
    RTRVec3 *vertices = NULL;
    RTRTriangle *triangles = NULL;
    float sourceMin[3];
    float sourceMax[3];
    uint32_t triangleCount = 0u;
    char line[256];

    *outTriangles = NULL;
    *outTriangleCount = 0u;

    if (!file) {
        fprintf(stderr, "scene: failed to open %s\n", RTR_SCENE_MESH_PATH);
        return 1;
    }

    if (rtrParsePlyHeader(file, &vertexCount, &faceCount)) {
        fprintf(stderr, "scene: failed to parse mesh PLY header\n");
        fclose(file);
        return 1;
    }

    if (faceCount > RTR_SCENE_TRIANGLE_CAPACITY) {
        fprintf(stderr,
                "scene: mesh has %u faces but capacity is %u\n",
                faceCount,
                (uint32_t)RTR_SCENE_TRIANGLE_CAPACITY);
        fclose(file);
        return 1;
    }

    vertices = (RTRVec3 *)malloc(sizeof(*vertices) * (size_t)vertexCount);
    triangles = (RTRTriangle *)malloc(sizeof(*triangles) * (size_t)faceCount);
    if (!(vertices && triangles)) {
        fprintf(stderr, "scene: mesh allocation failed\n");
        free(vertices);
        free(triangles);
        fclose(file);
        return 1;
    }

    rtrBoundsReset(sourceMin, sourceMax);
    for (uint32_t i = 0u; i < vertexCount; i++) {
        RTRVec3 v;
        if (!rtrReadLine(file, line, sizeof(line)) ||
            sscanf(line, "%f %f %f", &v.x, &v.y, &v.z) != 3) {
            fprintf(stderr, "scene: failed to read mesh vertex %u\n", i);
            free(vertices);
            free(triangles);
            fclose(file);
            return 1;
        }

        vertices[i] = v;
        rtrBoundsExtendPoint(sourceMin, sourceMax, v);
    }

    {
        const float centerX = (sourceMin[0] + sourceMax[0]) * 0.5f;
        const float centerZ = (sourceMin[2] + sourceMax[2]) * 0.5f;
        const float height = sourceMax[1] - sourceMin[1];
        const float scale = height > 0.0f ? RTR_SCENE_MESH_HEIGHT / height : 1.0f;

        for (uint32_t i = 0u; i < vertexCount; i++) {
            vertices[i].x = (vertices[i].x - centerX) * scale;
            vertices[i].y = (vertices[i].y - sourceMin[1]) * scale + RTR_SCENE_PLANE_Y;
            vertices[i].z = (vertices[i].z - centerZ) * scale;
        }
    }

    for (uint32_t face = 0u; face < faceCount; face++) {
        unsigned corners = 0u;
        unsigned i0 = 0u;
        unsigned i1 = 0u;
        unsigned i2 = 0u;

        if (!rtrReadLine(file, line, sizeof(line)) ||
            sscanf(line, "%u %u %u %u", &corners, &i0, &i1, &i2) != 4) {
            fprintf(stderr, "scene: failed to read mesh face %u\n", face);
            free(vertices);
            free(triangles);
            fclose(file);
            return 1;
        }

        if (corners != 3u ||
            i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
            fprintf(stderr, "scene: invalid mesh face %u\n", face);
            free(vertices);
            free(triangles);
            fclose(file);
            return 1;
        }

        triangles[triangleCount] = (RTRTriangle){
            .v0 = vertices[i0],
            .v1 = vertices[i1],
            .v2 = vertices[i2],
        };
        rtrTriangleFinalize(triangles + triangleCount);
        triangleCount++;
    }

    fclose(file);
    free(vertices);

    *outTriangles = triangles;
    *outTriangleCount = triangleCount;
    return 0;
}

static void rtrPackTriangle16(uint32_t packed[5], const uint32_t q[9])
{
    packed[0] = rtrPackU16(q[0], q[1]);
    packed[1] = rtrPackU16(q[2], q[3]);
    packed[2] = rtrPackU16(q[4], q[5]);
    packed[3] = rtrPackU16(q[6], q[7]);
    packed[4] = q[8] & 0xffffu;
}

static void rtrStoreTriangle(uint32_t *words,
                             uint32_t index,
                             const RTRTriangle *triangle,
                             const float sceneMin[3],
                             const float sceneMax[3])
{
    const uint32_t word = RTR_SCENE_GEOM_WORD + index * RTR_SCENE_TRIANGLE_WORDS;
    const float values[9] = {
        triangle->v0.x, triangle->v0.y, triangle->v0.z,
        triangle->v1.x, triangle->v1.y, triangle->v1.z,
        triangle->v2.x, triangle->v2.y, triangle->v2.z,
    };
    uint32_t q[9];
    uint32_t packed[5];

    for (uint32_t i = 0u; i < 9u; i++) {
        const uint32_t axis = i % 3u;
        q[i] = rtrQuantizeUnormNearest(values[i],
                                       sceneMin[axis],
                                       sceneMax[axis],
                                       RTR_SCENE_TRIANGLE_QUANT_MAX);
    }

    rtrPackTriangle16(packed, q);
    words[word + 0u] = packed[0];
    words[word + 1u] = packed[1];
    words[word + 2u] = packed[2];
    words[word + 3u] = packed[3];
    words[word + 4u] = packed[4];
}

static void rtrStoreBVHNode(uint32_t *words,
                            uint32_t index,
                            const RTRBVHNode *node)
{
    const uint32_t word = RTR_SCENE_BVH_WORD + index * RTR_SCENE_BVH_NODE_WORDS;

    words[word + 0u] = rtrF32Word(node->boundsMin[0]);
    words[word + 1u] = rtrF32Word(node->boundsMin[1]);
    words[word + 2u] = rtrF32Word(node->boundsMin[2]);
    words[word + 3u] = node->leftFirst;
    words[word + 4u] = rtrF32Word(node->boundsMax[0]);
    words[word + 5u] = rtrF32Word(node->boundsMax[1]);
    words[word + 6u] = rtrF32Word(node->boundsMax[2]);
    words[word + 7u] = node->countRight;
}

static void rtrStoreSceneQuantBounds(uint32_t *words,
                                     const float sceneMin[3],
                                     const float sceneMax[3])
{
    for (uint32_t axis = 0u; axis < 3u; axis++) {
        words[RTR_SCENE_QUANT_MIN_WORD + axis] = rtrF32Word(sceneMin[axis]);
        words[RTR_SCENE_QUANT_MAX_WORD + axis] = rtrF32Word(sceneMax[axis]);
    }
}

void rtrScene(uint32_t *words)
{
    RTRTriangle *triangles = NULL;
    RTRBVHNode *nodes = NULL;
    uint32_t triangleCount = 0u;
    uint32_t nodeCount = 0u;
    float sceneMin[3];
    float sceneMax[3];

    words[RTR_SCENE_COUNT_WORD] = 0u;
    words[RTR_SCENE_BVH_NODE_COUNT_WORD] = 0u;

    if (rtrLoadMeshTriangles(&triangles, &triangleCount) || triangleCount == 0u) {
        rtrStoreBlueNoise(words);
        rtrStoreEnvironment(words);
        free(triangles);
        return;
    }

    nodes = (RTRBVHNode *)malloc(
        sizeof(*nodes) * (size_t)(triangleCount * 2u - 1u));
    if (!nodes) {
        fprintf(stderr, "scene: BVH allocation failed\n");
        rtrStoreBlueNoise(words);
        rtrStoreEnvironment(words);
        free(triangles);
        return;
    }

    rtrBuildBVH(triangles, nodes, &nodeCount, 0u, triangleCount);
    memcpy(sceneMin, nodes[0].boundsMin, sizeof(sceneMin));
    memcpy(sceneMax, nodes[0].boundsMax, sizeof(sceneMax));

    words[RTR_SCENE_COUNT_WORD] = triangleCount;
    words[RTR_SCENE_BVH_NODE_COUNT_WORD] = nodeCount;
    rtrStoreSceneQuantBounds(words, sceneMin, sceneMax);

    for (uint32_t i = 0u; i < triangleCount; i++)
        rtrStoreTriangle(words, i, triangles + i, sceneMin, sceneMax);

    for (uint32_t i = 0u; i < nodeCount; i++)
        rtrStoreBVHNode(words, i, nodes + i);

    rtrStoreBlueNoise(words);
    rtrStoreEnvironment(words);

    free(triangles);
    free(nodes);
}
