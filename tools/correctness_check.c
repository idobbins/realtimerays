#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define RTR_CHECK_HEADER_WORDS 24u
#define RTR_CHECK_SPHERE_COUNT 1000000u
#define RTR_CHECK_SPHERE_WORDS 1u
#define RTR_CHECK_BVH_NODE_COUNT (RTR_CHECK_SPHERE_COUNT * 2u - 1u)
#define RTR_CHECK_BVH_NODE_WORDS 5u
#define RTR_CHECK_BLUE_NOISE_WORDS (64u * 64u)
#define RTR_CHECK_WORDS \
    (RTR_CHECK_HEADER_WORDS + \
     RTR_CHECK_SPHERE_COUNT * RTR_CHECK_SPHERE_WORDS + \
     RTR_CHECK_BVH_NODE_COUNT * RTR_CHECK_BVH_NODE_WORDS + \
     RTR_CHECK_BLUE_NOISE_WORDS)

#define RTR_CHECK_SPHERE_COUNT_WORD 8u
#define RTR_CHECK_BVH_NODE_COUNT_WORD 15u
#define RTR_CHECK_SPHERE_WORD 24u
#define RTR_CHECK_BVH_NODE_WORD \
    (RTR_CHECK_SPHERE_WORD + RTR_CHECK_SPHERE_COUNT * RTR_CHECK_SPHERE_WORDS)
#define RTR_CHECK_BVH_INTERNAL_FLAG 0x80000000u
#define RTR_CHECK_GRID_EMPTY UINT32_MAX
#define RTR_CHECK_STACK_SIZE 64

#define RTR_CHECK_DISK_RADIUS 26.0f
#define RTR_CHECK_MAX_SPHERE_RADIUS 0.025f
#define RTR_CHECK_PLANE_Y -1.0f
#define RTR_CHECK_EPS 0.0001f
#define RTR_CHECK_INF 1.0e20f
#define RTR_CHECK_UNORM16_SCALE (1.0f / 65535.0f)
#define RTR_CHECK_UNORM12_SCALE (1.0f / 4095.0f)
#define RTR_CHECK_UNORM8_SCALE (1.0f / 255.0f)

void rtrScene(uint32_t *words);

typedef struct RTRCheckVec3 {
    float x;
    float y;
    float z;
} RTRCheckVec3;

typedef struct RTRCheckSphere {
    float x;
    float y;
    float z;
    float r;
} RTRCheckSphere;

typedef struct RTRCheckHit {
    float t;
    int32_t id;
} RTRCheckHit;

static uint32_t rtrCheckHashU32(uint32_t value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

static float rtrCheckHash01(uint32_t value)
{
    return (float)(rtrCheckHashU32(value) & 0x00ffffffu) / 16777215.0f;
}

static float rtrCheckUnpackUnorm16(uint32_t value, float minValue, float maxValue)
{
    return minValue + (maxValue - minValue) *
        ((float)(value & 0xffffu) * RTR_CHECK_UNORM16_SCALE);
}

static float rtrCheckUnpackUnormN(uint32_t value,
                                  uint32_t mask,
                                  float scale,
                                  float minValue,
                                  float maxValue)
{
    return minValue + (maxValue - minValue) * ((float)(value & mask) * scale);
}

static RTRCheckSphere rtrCheckLoadSphere(const uint32_t *words, uint32_t i)
{
    const uint32_t packed = words[RTR_CHECK_SPHERE_WORD + i * RTR_CHECK_SPHERE_WORDS];
    const float x = rtrCheckUnpackUnormN(packed, 0xfffu, RTR_CHECK_UNORM12_SCALE,
                                         -RTR_CHECK_DISK_RADIUS,
                                         RTR_CHECK_DISK_RADIUS);
    const float z = rtrCheckUnpackUnormN(packed >> 12u, 0xfffu, RTR_CHECK_UNORM12_SCALE,
                                         -RTR_CHECK_DISK_RADIUS,
                                         RTR_CHECK_DISK_RADIUS);
    const float r = rtrCheckUnpackUnormN(packed >> 24u, 0xffu, RTR_CHECK_UNORM8_SCALE,
                                         0.0f, RTR_CHECK_MAX_SPHERE_RADIUS);

    return (RTRCheckSphere){x, RTR_CHECK_PLANE_Y + r, z, r};
}

static RTRCheckVec3 rtrCheckLoadBVHMin(const uint32_t *words, uint32_t i)
{
    const uint32_t word = RTR_CHECK_BVH_NODE_WORD + i * RTR_CHECK_BVH_NODE_WORDS;
    const uint32_t xz = words[word];
    const uint32_t yy = words[word + 2u];

    return (RTRCheckVec3){
        rtrCheckUnpackUnorm16(xz, -RTR_CHECK_DISK_RADIUS, RTR_CHECK_DISK_RADIUS),
        rtrCheckUnpackUnorm16(yy, RTR_CHECK_PLANE_Y,
                              RTR_CHECK_PLANE_Y + RTR_CHECK_MAX_SPHERE_RADIUS * 2.0f),
        rtrCheckUnpackUnorm16(xz >> 16u, -RTR_CHECK_DISK_RADIUS, RTR_CHECK_DISK_RADIUS),
    };
}

static RTRCheckVec3 rtrCheckLoadBVHMax(const uint32_t *words, uint32_t i)
{
    const uint32_t word = RTR_CHECK_BVH_NODE_WORD + i * RTR_CHECK_BVH_NODE_WORDS;
    const uint32_t xz = words[word + 1u];
    const uint32_t yy = words[word + 2u];

    return (RTRCheckVec3){
        rtrCheckUnpackUnorm16(xz, -RTR_CHECK_DISK_RADIUS, RTR_CHECK_DISK_RADIUS),
        rtrCheckUnpackUnorm16(yy >> 16u, RTR_CHECK_PLANE_Y,
                              RTR_CHECK_PLANE_Y + RTR_CHECK_MAX_SPHERE_RADIUS * 2.0f),
        rtrCheckUnpackUnorm16(xz >> 16u, -RTR_CHECK_DISK_RADIUS, RTR_CHECK_DISK_RADIUS),
    };
}

static float rtrCheckDot(RTRCheckVec3 a, RTRCheckVec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static RTRCheckVec3 rtrCheckSub(RTRCheckVec3 a, RTRCheckVec3 b)
{
    return (RTRCheckVec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static RTRCheckVec3 rtrCheckNormalize(RTRCheckVec3 v)
{
    const float len2 = rtrCheckDot(v, v);
    const float invLen = len2 > 0.0f ? 1.0f / sqrtf(len2) : 0.0f;
    return (RTRCheckVec3){v.x * invLen, v.y * invLen, v.z * invLen};
}

static RTRCheckVec3 rtrCheckSafeInvDir(RTRCheckVec3 rd)
{
    const float ax = fabsf(rd.x) > 1.0e-8f ? fabsf(rd.x) : 1.0e-8f;
    const float ay = fabsf(rd.y) > 1.0e-8f ? fabsf(rd.y) : 1.0e-8f;
    const float az = fabsf(rd.z) > 1.0e-8f ? fabsf(rd.z) : 1.0e-8f;

    return (RTRCheckVec3){
        1.0f / (rd.x >= 0.0f ? ax : -ax),
        1.0f / (rd.y >= 0.0f ? ay : -ay),
        1.0f / (rd.z >= 0.0f ? az : -az),
    };
}

static uint32_t rtrCheckBoxHit(RTRCheckVec3 ro,
                               RTRCheckVec3 invRd,
                               RTRCheckVec3 boundsMin,
                               RTRCheckVec3 boundsMax,
                               float maxT,
                               float *nearT)
{
    const float tx0 = (boundsMin.x - ro.x) * invRd.x;
    const float ty0 = (boundsMin.y - ro.y) * invRd.y;
    const float tz0 = (boundsMin.z - ro.z) * invRd.z;
    const float tx1 = (boundsMax.x - ro.x) * invRd.x;
    const float ty1 = (boundsMax.y - ro.y) * invRd.y;
    const float tz1 = (boundsMax.z - ro.z) * invRd.z;
    const float lx = tx0 < tx1 ? tx0 : tx1;
    const float ly = ty0 < ty1 ? ty0 : ty1;
    const float lz = tz0 < tz1 ? tz0 : tz1;
    const float hx = tx0 > tx1 ? tx0 : tx1;
    const float hy = ty0 > ty1 ? ty0 : ty1;
    const float hz = tz0 > tz1 ? tz0 : tz1;
    const float lo = fmaxf(fmaxf(lx, ly), fmaxf(lz, RTR_CHECK_EPS));
    const float hi = fminf(fminf(hx, hy), fminf(hz, maxT));

    *nearT = lo;
    return (uint32_t)(lo <= hi);
}

static void rtrCheckTraceSphere(RTRCheckVec3 ro,
                                RTRCheckVec3 rd,
                                const RTRCheckSphere *sphere,
                                uint32_t id,
                                RTRCheckHit *hit)
{
    const RTRCheckVec3 center = {sphere->x, sphere->y, sphere->z};
    const RTRCheckVec3 oc = rtrCheckSub(ro, center);
    const float b = rtrCheckDot(oc, rd);
    const float centerT = -b;
    const float r = sphere->r;
    float h = 0.0f;
    float t = 0.0f;

    if (centerT + r < RTR_CHECK_EPS || centerT - r >= hit->t)
        return;

    h = b * b - (rtrCheckDot(oc, oc) - r * r);
    if (h < 0.0f)
        return;

    t = centerT - sqrtf(h);
    if (t < RTR_CHECK_EPS)
        t = centerT + sqrtf(h);

    if (t >= RTR_CHECK_EPS && t < hit->t) {
        hit->t = t;
        hit->id = (int32_t)id;
    }
}

static RTRCheckHit rtrCheckTraceBrute(RTRCheckVec3 ro,
                                      RTRCheckVec3 rd,
                                      const RTRCheckSphere *spheres,
                                      uint32_t sphereCount)
{
    RTRCheckHit hit = {RTR_CHECK_INF, -1};

    for (uint32_t i = 0u; i < sphereCount; i++)
        rtrCheckTraceSphere(ro, rd, spheres + i, i, &hit);

    return hit;
}

static RTRCheckHit rtrCheckTraceBVH(RTRCheckVec3 ro,
                                    RTRCheckVec3 rd,
                                    const uint32_t *words,
                                    const RTRCheckSphere *spheres,
                                    uint32_t sphereCount,
                                    uint32_t nodeCount)
{
    RTRCheckHit hit = {RTR_CHECK_INF, -1};
    RTRCheckVec3 invRd = rtrCheckSafeInvDir(rd);
    uint32_t stack[RTR_CHECK_STACK_SIZE];
    int32_t stackCount = 0;
    uint32_t nodeIndex = 0u;
    float rootNear = RTR_CHECK_INF;

    if (nodeCount == 0u ||
        !rtrCheckBoxHit(ro, invRd, rtrCheckLoadBVHMin(words, 0u),
                        rtrCheckLoadBVHMax(words, 0u), hit.t, &rootNear)) {
        return hit;
    }

    for (;;) {
        const uint32_t word = RTR_CHECK_BVH_NODE_WORD +
            nodeIndex * RTR_CHECK_BVH_NODE_WORDS;
        const uint32_t leftFirst = words[word + 3u];
        const uint32_t countRight = words[word + 4u];

        if ((countRight & RTR_CHECK_BVH_INTERNAL_FLAG) != 0u) {
            const uint32_t left = leftFirst;
            const uint32_t right = countRight & ~RTR_CHECK_BVH_INTERNAL_FLAG;
            float leftNear = RTR_CHECK_INF;
            float rightNear = RTR_CHECK_INF;
            const uint32_t leftHit = left < nodeCount &&
                rtrCheckBoxHit(ro, invRd, rtrCheckLoadBVHMin(words, left),
                               rtrCheckLoadBVHMax(words, left), hit.t, &leftNear);
            const uint32_t rightHit = right < nodeCount &&
                rtrCheckBoxHit(ro, invRd, rtrCheckLoadBVHMin(words, right),
                               rtrCheckLoadBVHMax(words, right), hit.t, &rightNear);

            if (leftHit && rightHit) {
                if (leftNear <= rightNear) {
                    if (stackCount >= RTR_CHECK_STACK_SIZE)
                        return (RTRCheckHit){RTR_CHECK_INF, -2};
                    stack[stackCount++] = right;
                    nodeIndex = left;
                } else {
                    if (stackCount >= RTR_CHECK_STACK_SIZE)
                        return (RTRCheckHit){RTR_CHECK_INF, -2};
                    stack[stackCount++] = left;
                    nodeIndex = right;
                }
                continue;
            }

            if (leftHit) {
                nodeIndex = left;
                continue;
            }
            if (rightHit) {
                nodeIndex = right;
                continue;
            }
        } else {
            for (uint32_t i = 0u; i < countRight; i++) {
                const uint32_t sphereId = leftFirst + i;
                if (sphereId < sphereCount)
                    rtrCheckTraceSphere(ro, rd, spheres + sphereId, sphereId, &hit);
            }
        }

        if (stackCount == 0)
            break;
        nodeIndex = stack[--stackCount];
    }

    return hit;
}

static uint32_t rtrCheckGridCoord(float value, uint32_t dim, float cellSize)
{
    int32_t coord = (int32_t)floorf((value + RTR_CHECK_DISK_RADIUS) / cellSize);

    if (coord < 0) return 0u;
    if (coord >= (int32_t)dim) return dim - 1u;
    return (uint32_t)coord;
}

static int rtrCheckSceneGeometry(const RTRCheckSphere *spheres, uint32_t sphereCount)
{
    const float cellSize = RTR_CHECK_MAX_SPHERE_RADIUS * 2.0f;
    const uint32_t dim =
        (uint32_t)ceilf((RTR_CHECK_DISK_RADIUS * 2.0f) / cellSize) + 1u;
    const uint64_t cellCount = (uint64_t)dim * (uint64_t)dim;
    const float overlapLimit = RTR_CHECK_MAX_SPHERE_RADIUS * 2.0f;
    const uint32_t cellRange = (uint32_t)ceilf(overlapLimit / cellSize) + 1u;
    uint32_t *heads = (uint32_t *)malloc(sizeof(*heads) * (size_t)cellCount);
    uint32_t *next = (uint32_t *)malloc(sizeof(*next) * (size_t)sphereCount);
    uint32_t overlapPairs = 0u;
    uint32_t boundaryFailures = 0u;
    float worstMargin = FLT_MAX;

    if (!(heads && next)) {
        free(heads);
        free(next);
        fprintf(stderr, "correctness: failed to allocate geometry grid\n");
        return 1;
    }

    for (uint64_t i = 0u; i < cellCount; i++)
        heads[i] = RTR_CHECK_GRID_EMPTY;
    for (uint32_t i = 0u; i < sphereCount; i++)
        next[i] = RTR_CHECK_GRID_EMPTY;

    for (uint32_t i = 0u; i < sphereCount; i++) {
        const uint32_t centerX = rtrCheckGridCoord(spheres[i].x, dim, cellSize);
        const uint32_t centerZ = rtrCheckGridCoord(spheres[i].z, dim, cellSize);
        const float diskDistance = sqrtf(spheres[i].x * spheres[i].x +
                                         spheres[i].z * spheres[i].z);
        int32_t minX = (int32_t)centerX - (int32_t)cellRange;
        int32_t minZ = (int32_t)centerZ - (int32_t)cellRange;
        int32_t maxX = (int32_t)centerX + (int32_t)cellRange;
        int32_t maxZ = (int32_t)centerZ + (int32_t)cellRange;

        if (diskDistance + spheres[i].r > RTR_CHECK_DISK_RADIUS + 0.0002f)
            boundaryFailures++;

        if (minX < 0) minX = 0;
        if (minZ < 0) minZ = 0;
        if (maxX >= (int32_t)dim) maxX = (int32_t)dim - 1;
        if (maxZ >= (int32_t)dim) maxZ = (int32_t)dim - 1;

        for (int32_t cellZ = minZ; cellZ <= maxZ; cellZ++) {
            for (int32_t cellX = minX; cellX <= maxX; cellX++) {
                uint32_t j = heads[(uint64_t)cellZ * (uint64_t)dim + (uint64_t)cellX];

                while (j != RTR_CHECK_GRID_EMPTY) {
                    const float dx = spheres[i].x - spheres[j].x;
                    const float dz = spheres[i].z - spheres[j].z;
                    const float dist = sqrtf(dx * dx + dz * dz);
                    const float margin = dist - (spheres[i].r + spheres[j].r);

                    if (margin < worstMargin)
                        worstMargin = margin;
                    if (margin < -0.00001f)
                        overlapPairs++;

                    j = next[j];
                }
            }
        }

        next[i] = heads[(uint64_t)centerZ * (uint64_t)dim + (uint64_t)centerX];
        heads[(uint64_t)centerZ * (uint64_t)dim + (uint64_t)centerX] = i;
    }

    free(heads);
    free(next);

    if (overlapPairs || boundaryFailures) {
        fprintf(stderr,
                "correctness: geometry failed: overlaps=%u boundary=%u worst_margin=%g\n",
                overlapPairs, boundaryFailures, worstMargin);
        return 1;
    }

    printf("geometry: ok, worst packed margin %.6f\n", worstMargin);
    return 0;
}

static int rtrCheckBVH(const uint32_t *words,
                       const RTRCheckSphere *spheres,
                       uint32_t sphereCount,
                       uint32_t nodeCount)
{
    uint32_t failures = 0u;

    for (uint32_t i = 0u; i < 96u; i++) {
        RTRCheckVec3 ro;
        RTRCheckVec3 target;
        RTRCheckVec3 rd;
        RTRCheckHit brute;
        RTRCheckHit bvh;

        ro.x = (rtrCheckHash01(i * 747796405u + 0x10203040u) - 0.5f) * 14.0f;
        ro.y = 0.15f + rtrCheckHash01(i * 2891336453u + 0x50607080u) * 4.5f;
        ro.z = (rtrCheckHash01(i * 2246822519u + 0x90abcdefu) - 0.5f) * 14.0f;
        target.x = (rtrCheckHash01(i * 3266489917u + 0x13579bdfu) - 0.5f) * 24.0f;
        target.y = RTR_CHECK_PLANE_Y +
            rtrCheckHash01(i * 668265263u + 0x2468ace0u) * 0.08f;
        target.z = (rtrCheckHash01(i * 374761393u + 0xfedcba09u) - 0.5f) * 24.0f;
        rd = rtrCheckNormalize(rtrCheckSub(target, ro));

        brute = rtrCheckTraceBrute(ro, rd, spheres, sphereCount);
        bvh = rtrCheckTraceBVH(ro, rd, words, spheres, sphereCount, nodeCount);

        if (bvh.id == -2 ||
            ((brute.id < 0) != (bvh.id < 0)) ||
            (brute.id >= 0 && fabsf(brute.t - bvh.t) > 0.002f)) {
            fprintf(stderr,
                    "correctness: bvh mismatch ray=%u brute=(%d,%g) bvh=(%d,%g)\n",
                    i, brute.id, brute.t, bvh.id, bvh.t);
            failures++;
            if (failures >= 8u)
                break;
        }
    }

    if (failures)
        return 1;

    printf("bvh: ok, 96 deterministic rays matched brute force\n");
    return 0;
}

int main(void)
{
    uint32_t *words = (uint32_t *)calloc((size_t)RTR_CHECK_WORDS, sizeof(*words));
    RTRCheckSphere *spheres =
        (RTRCheckSphere *)malloc(sizeof(*spheres) * (size_t)RTR_CHECK_SPHERE_COUNT);
    uint32_t sphereCount = 0u;
    uint32_t nodeCount = 0u;
    int failed = 0;

    if (!(words && spheres)) {
        fprintf(stderr, "correctness: allocation failed\n");
        free(words);
        free(spheres);
        return 1;
    }

    rtrScene(words);
    sphereCount = words[RTR_CHECK_SPHERE_COUNT_WORD];
    nodeCount = words[RTR_CHECK_BVH_NODE_COUNT_WORD];

    if (sphereCount != RTR_CHECK_SPHERE_COUNT || nodeCount == 0u ||
        nodeCount > RTR_CHECK_BVH_NODE_COUNT) {
        fprintf(stderr, "correctness: invalid counts spheres=%u nodes=%u\n",
                sphereCount, nodeCount);
        free(words);
        free(spheres);
        return 1;
    }

    for (uint32_t i = 0u; i < sphereCount; i++)
        spheres[i] = rtrCheckLoadSphere(words, i);

    failed |= rtrCheckSceneGeometry(spheres, sphereCount);
    failed |= rtrCheckBVH(words, spheres, sphereCount, nodeCount);

    free(words);
    free(spheres);
    return failed ? 1 : 0;
}
