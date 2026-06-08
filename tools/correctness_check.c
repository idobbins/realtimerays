#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RTR_CHECK_HEADER_WORDS 32u
#define RTR_CHECK_TRIANGLE_CAPACITY 900000u
#define RTR_CHECK_TRIANGLE_WORDS 5u
#define RTR_CHECK_BVH_NODE_COUNT (RTR_CHECK_TRIANGLE_CAPACITY * 2u - 1u)
#define RTR_CHECK_BVH_NODE_WORDS 8u
#define RTR_CHECK_BLUE_NOISE_WORDS (64u * 64u)
#define RTR_CHECK_ENVMAP_WORDS (1024u * 512u * 2u)
#define RTR_CHECK_ENVMAP_DIFFUSE_WORDS (32u * 16u * 2u)
#define RTR_CHECK_WORDS \
    (RTR_CHECK_HEADER_WORDS + \
     RTR_CHECK_TRIANGLE_CAPACITY * RTR_CHECK_TRIANGLE_WORDS + \
     RTR_CHECK_BVH_NODE_COUNT * RTR_CHECK_BVH_NODE_WORDS + \
     RTR_CHECK_BLUE_NOISE_WORDS + \
     RTR_CHECK_ENVMAP_WORDS + \
     RTR_CHECK_ENVMAP_DIFFUSE_WORDS)

#define RTR_CHECK_TRIANGLE_COUNT_WORD 8u
#define RTR_CHECK_BVH_NODE_COUNT_WORD 15u
#define RTR_CHECK_SCENE_QUANT_MIN_WORD 24u
#define RTR_CHECK_SCENE_QUANT_MAX_WORD 27u
#define RTR_CHECK_TRIANGLE_WORD 32u
#define RTR_CHECK_BVH_NODE_WORD \
    (RTR_CHECK_TRIANGLE_WORD + RTR_CHECK_TRIANGLE_CAPACITY * RTR_CHECK_TRIANGLE_WORDS)
#define RTR_CHECK_BVH_INTERNAL_FLAG 0x80000000u
#define RTR_CHECK_STACK_SIZE 64
#define RTR_CHECK_EPS 0.0001f
#define RTR_CHECK_CONTAINMENT_EPS 0.000001f
#define RTR_CHECK_INF 1.0e20f
#define RTR_CHECK_TRIANGLE_QUANT_MAX 65535u

void rtrScene(uint32_t *words);

typedef struct RTRCheckVec3 {
    float x;
    float y;
    float z;
} RTRCheckVec3;

typedef struct RTRCheckTriangle {
    RTRCheckVec3 v0;
    RTRCheckVec3 v1;
    RTRCheckVec3 v2;
} RTRCheckTriangle;

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

static float rtrCheckLoadF32(const uint32_t *words, uint32_t word)
{
    float value = 0.0f;
    const uint32_t bits = words[word];
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t rtrCheckLo16(uint32_t value)
{
    return value & 0xffffu;
}

static uint32_t rtrCheckHi16(uint32_t value)
{
    return value >> 16u;
}

static float rtrCheckDequantizeUnorm(uint32_t value,
                                     float minValue,
                                     float maxValue,
                                     uint32_t maxQuant)
{
    return minValue +
        (maxValue - minValue) * ((float)value / (float)maxQuant);
}

static RTRCheckVec3 rtrCheckAdd(RTRCheckVec3 a, RTRCheckVec3 b)
{
    return (RTRCheckVec3){a.x + b.x, a.y + b.y, a.z + b.z};
}

static RTRCheckVec3 rtrCheckSub(RTRCheckVec3 a, RTRCheckVec3 b)
{
    return (RTRCheckVec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static RTRCheckVec3 rtrCheckMul(RTRCheckVec3 v, float s)
{
    return (RTRCheckVec3){v.x * s, v.y * s, v.z * s};
}

static uint32_t rtrCheckVec3Finite(RTRCheckVec3 v)
{
    return (uint32_t)(isfinite(v.x) && isfinite(v.y) && isfinite(v.z));
}

static float rtrCheckPointBoundsSlop(RTRCheckVec3 p,
                                     RTRCheckVec3 boundsMin,
                                     RTRCheckVec3 boundsMax)
{
    float slop = 0.0f;

    slop = fmaxf(slop, boundsMin.x - p.x);
    slop = fmaxf(slop, p.x - boundsMax.x);
    slop = fmaxf(slop, boundsMin.y - p.y);
    slop = fmaxf(slop, p.y - boundsMax.y);
    slop = fmaxf(slop, boundsMin.z - p.z);
    slop = fmaxf(slop, p.z - boundsMax.z);

    return slop;
}

static float rtrCheckBoundsContainSlop(RTRCheckVec3 outerMin,
                                       RTRCheckVec3 outerMax,
                                       RTRCheckVec3 innerMin,
                                       RTRCheckVec3 innerMax)
{
    float slop = 0.0f;

    slop = fmaxf(slop, rtrCheckPointBoundsSlop(innerMin, outerMin, outerMax));
    slop = fmaxf(slop, rtrCheckPointBoundsSlop(innerMax, outerMin, outerMax));

    return slop;
}

static float rtrCheckDot(RTRCheckVec3 a, RTRCheckVec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static RTRCheckVec3 rtrCheckCross(RTRCheckVec3 a, RTRCheckVec3 b)
{
    return (RTRCheckVec3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

static RTRCheckVec3 rtrCheckNormalize(RTRCheckVec3 v)
{
    const float len2 = rtrCheckDot(v, v);
    const float invLen = len2 > 0.0f ? 1.0f / sqrtf(len2) : 0.0f;
    return rtrCheckMul(v, invLen);
}

static RTRCheckVec3 rtrCheckLoadSceneQuantMin(const uint32_t *words)
{
    return (RTRCheckVec3){
        rtrCheckLoadF32(words, RTR_CHECK_SCENE_QUANT_MIN_WORD),
        rtrCheckLoadF32(words, RTR_CHECK_SCENE_QUANT_MIN_WORD + 1u),
        rtrCheckLoadF32(words, RTR_CHECK_SCENE_QUANT_MIN_WORD + 2u),
    };
}

static RTRCheckVec3 rtrCheckLoadSceneQuantMax(const uint32_t *words)
{
    return (RTRCheckVec3){
        rtrCheckLoadF32(words, RTR_CHECK_SCENE_QUANT_MAX_WORD),
        rtrCheckLoadF32(words, RTR_CHECK_SCENE_QUANT_MAX_WORD + 1u),
        rtrCheckLoadF32(words, RTR_CHECK_SCENE_QUANT_MAX_WORD + 2u),
    };
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

static RTRCheckTriangle rtrCheckLoadTriangle(const uint32_t *words,
                                             uint32_t i)
{
    const uint32_t word = RTR_CHECK_TRIANGLE_WORD + i * RTR_CHECK_TRIANGLE_WORDS;
    const uint32_t w0 = words[word];
    const uint32_t w1 = words[word + 1u];
    const uint32_t w2 = words[word + 2u];
    const uint32_t w3 = words[word + 3u];
    const uint32_t w4 = words[word + 4u];
    const RTRCheckVec3 sceneMin = rtrCheckLoadSceneQuantMin(words);
    const RTRCheckVec3 sceneMax = rtrCheckLoadSceneQuantMax(words);

    return (RTRCheckTriangle){
        .v0 = {
            rtrCheckDequantizeUnorm(rtrCheckLo16(w0),
                                    sceneMin.x,
                                    sceneMax.x,
                                    RTR_CHECK_TRIANGLE_QUANT_MAX),
            rtrCheckDequantizeUnorm(rtrCheckHi16(w0),
                                    sceneMin.y,
                                    sceneMax.y,
                                    RTR_CHECK_TRIANGLE_QUANT_MAX),
            rtrCheckDequantizeUnorm(rtrCheckLo16(w1),
                                    sceneMin.z,
                                    sceneMax.z,
                                    RTR_CHECK_TRIANGLE_QUANT_MAX),
        },
        .v1 = {
            rtrCheckDequantizeUnorm(rtrCheckHi16(w1),
                                    sceneMin.x,
                                    sceneMax.x,
                                    RTR_CHECK_TRIANGLE_QUANT_MAX),
            rtrCheckDequantizeUnorm(rtrCheckLo16(w2),
                                    sceneMin.y,
                                    sceneMax.y,
                                    RTR_CHECK_TRIANGLE_QUANT_MAX),
            rtrCheckDequantizeUnorm(rtrCheckHi16(w2),
                                    sceneMin.z,
                                    sceneMax.z,
                                    RTR_CHECK_TRIANGLE_QUANT_MAX),
        },
        .v2 = {
            rtrCheckDequantizeUnorm(rtrCheckLo16(w3),
                                    sceneMin.x,
                                    sceneMax.x,
                                    RTR_CHECK_TRIANGLE_QUANT_MAX),
            rtrCheckDequantizeUnorm(rtrCheckHi16(w3),
                                    sceneMin.y,
                                    sceneMax.y,
                                    RTR_CHECK_TRIANGLE_QUANT_MAX),
            rtrCheckDequantizeUnorm(rtrCheckLo16(w4),
                                    sceneMin.z,
                                    sceneMax.z,
                                    RTR_CHECK_TRIANGLE_QUANT_MAX),
        },
    };
}

static RTRCheckVec3 rtrCheckLoadBVHMin(const uint32_t *words, uint32_t i)
{
    const uint32_t word = RTR_CHECK_BVH_NODE_WORD + i * RTR_CHECK_BVH_NODE_WORDS;

    return (RTRCheckVec3){
        rtrCheckLoadF32(words, word),
        rtrCheckLoadF32(words, word + 1u),
        rtrCheckLoadF32(words, word + 2u),
    };
}

static RTRCheckVec3 rtrCheckLoadBVHMax(const uint32_t *words, uint32_t i)
{
    const uint32_t word = RTR_CHECK_BVH_NODE_WORD + i * RTR_CHECK_BVH_NODE_WORDS;

    return (RTRCheckVec3){
        rtrCheckLoadF32(words, word + 4u),
        rtrCheckLoadF32(words, word + 5u),
        rtrCheckLoadF32(words, word + 6u),
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

static void rtrCheckTraceTriangle(RTRCheckVec3 ro,
                                  RTRCheckVec3 rd,
                                  const RTRCheckTriangle *tri,
                                  uint32_t id,
                                  RTRCheckHit *hit)
{
    const RTRCheckVec3 e1 = rtrCheckSub(tri->v1, tri->v0);
    const RTRCheckVec3 e2 = rtrCheckSub(tri->v2, tri->v0);
    const RTRCheckVec3 pvec = rtrCheckCross(rd, e2);
    const float det = rtrCheckDot(e1, pvec);
    float invDet = 0.0f;
    RTRCheckVec3 tvec;
    float u = 0.0f;
    RTRCheckVec3 qvec;
    float v = 0.0f;
    float t = 0.0f;

    if (fabsf(det) < 1.0e-8f)
        return;

    invDet = 1.0f / det;
    tvec = rtrCheckSub(ro, tri->v0);
    u = rtrCheckDot(tvec, pvec) * invDet;
    if (u < 0.0f || u > 1.0f)
        return;

    qvec = rtrCheckCross(tvec, e1);
    v = rtrCheckDot(rd, qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f)
        return;

    t = rtrCheckDot(e2, qvec) * invDet;
    if (t >= RTR_CHECK_EPS && t < hit->t) {
        hit->t = t;
        hit->id = (int32_t)id;
    }
}

static RTRCheckHit rtrCheckTraceBrute(RTRCheckVec3 ro,
                                      RTRCheckVec3 rd,
                                      const RTRCheckTriangle *triangles,
                                      uint32_t triangleCount)
{
    RTRCheckHit hit = {RTR_CHECK_INF, -1};

    for (uint32_t i = 0u; i < triangleCount; i++)
        rtrCheckTraceTriangle(ro, rd, triangles + i, i, &hit);

    return hit;
}

static RTRCheckHit rtrCheckTraceBVH(RTRCheckVec3 ro,
                                    RTRCheckVec3 rd,
                                    const uint32_t *words,
                                    const RTRCheckTriangle *triangles,
                                    uint32_t triangleCount,
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
        const uint32_t countRight = words[word + 7u];

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
                const uint32_t triangleId = leftFirst + i;
                if (triangleId < triangleCount)
                    rtrCheckTraceTriangle(ro, rd, triangles + triangleId,
                                          triangleId, &hit);
            }
        }

        if (stackCount == 0)
            break;
        nodeIndex = stack[--stackCount];
    }

    return hit;
}

static int rtrCheckSceneGeometry(const RTRCheckTriangle *triangles,
                                 uint32_t triangleCount,
                                 RTRCheckVec3 rootMin,
                                 RTRCheckVec3 rootMax)
{
    uint32_t invalid = 0u;
    uint32_t outsideRoot = 0u;
    float minArea2 = FLT_MAX;

    for (uint32_t i = 0u; i < triangleCount; i++) {
        const RTRCheckTriangle *tri = triangles + i;
        const RTRCheckVec3 e1 = rtrCheckSub(tri->v1, tri->v0);
        const RTRCheckVec3 e2 = rtrCheckSub(tri->v2, tri->v0);
        const RTRCheckVec3 cross = rtrCheckCross(e1, e2);
        const float area2 = rtrCheckDot(cross, cross);
        const RTRCheckVec3 verts[3] = {tri->v0, tri->v1, tri->v2};

        if (area2 < minArea2)
            minArea2 = area2;
        if (!isfinite(area2))
            invalid++;

        for (uint32_t v = 0u; v < 3u; v++) {
            if (verts[v].x < rootMin.x - 0.001f || verts[v].x > rootMax.x + 0.001f ||
                verts[v].y < rootMin.y - 0.001f || verts[v].y > rootMax.y + 0.001f ||
                verts[v].z < rootMin.z - 0.001f || verts[v].z > rootMax.z + 0.001f) {
                outsideRoot++;
                break;
            }
        }
    }

    if (invalid || outsideRoot) {
        fprintf(stderr,
                "correctness: geometry failed invalid=%u outside_root=%u min_area2=%g\n",
                invalid, outsideRoot, minArea2);
        return 1;
    }

    printf("geometry: ok, %u triangles root [(%.3f %.3f %.3f) (%.3f %.3f %.3f)] min_area2 %.3g\n",
           triangleCount,
           rootMin.x, rootMin.y, rootMin.z,
           rootMax.x, rootMax.y, rootMax.z,
           minArea2);
    return 0;
}

static int rtrCheckBVHContainment(const uint32_t *words,
                                  const RTRCheckTriangle *triangles,
                                  uint32_t triangleCount,
                                  uint32_t nodeCount)
{
    uint8_t *triangleRefs =
        (uint8_t *)calloc((size_t)triangleCount, sizeof(*triangleRefs));
    uint32_t invalidBounds = 0u;
    uint32_t invalidLinks = 0u;
    uint32_t invalidLeaves = 0u;
    uint32_t outsideTriangles = 0u;
    uint32_t outsideChildren = 0u;
    uint32_t duplicateRefs = 0u;
    uint32_t coveredTriangles = 0u;
    uint32_t missingTriangles = 0u;
    uint32_t leafCount = 0u;
    float maxTriangleSlop = 0.0f;
    float maxChildSlop = 0.0f;

    if (!triangleRefs) {
        fprintf(stderr, "correctness: containment allocation failed\n");
        return 1;
    }

    for (uint32_t node = 0u; node < nodeCount; node++) {
        const uint32_t word = RTR_CHECK_BVH_NODE_WORD +
            node * RTR_CHECK_BVH_NODE_WORDS;
        const uint32_t leftFirst = words[word + 3u];
        const uint32_t countRight = words[word + 7u];
        const RTRCheckVec3 boundsMin = rtrCheckLoadBVHMin(words, node);
        const RTRCheckVec3 boundsMax = rtrCheckLoadBVHMax(words, node);

        if (!rtrCheckVec3Finite(boundsMin) ||
            !rtrCheckVec3Finite(boundsMax) ||
            boundsMin.x > boundsMax.x ||
            boundsMin.y > boundsMax.y ||
            boundsMin.z > boundsMax.z) {
            invalidBounds++;
            continue;
        }

        if ((countRight & RTR_CHECK_BVH_INTERNAL_FLAG) != 0u) {
            const uint32_t left = leftFirst;
            const uint32_t right = countRight & ~RTR_CHECK_BVH_INTERNAL_FLAG;
            RTRCheckVec3 childMin;
            RTRCheckVec3 childMax;
            float slop;

            if (left >= nodeCount || right >= nodeCount || left == right) {
                invalidLinks++;
                continue;
            }

            childMin = rtrCheckLoadBVHMin(words, left);
            childMax = rtrCheckLoadBVHMax(words, left);
            slop = rtrCheckBoundsContainSlop(boundsMin, boundsMax,
                                             childMin, childMax);
            if (slop > maxChildSlop)
                maxChildSlop = slop;
            if (slop > RTR_CHECK_CONTAINMENT_EPS)
                outsideChildren++;

            childMin = rtrCheckLoadBVHMin(words, right);
            childMax = rtrCheckLoadBVHMax(words, right);
            slop = rtrCheckBoundsContainSlop(boundsMin, boundsMax,
                                             childMin, childMax);
            if (slop > maxChildSlop)
                maxChildSlop = slop;
            if (slop > RTR_CHECK_CONTAINMENT_EPS)
                outsideChildren++;
        } else {
            if (countRight == 0u ||
                leftFirst >= triangleCount ||
                countRight > triangleCount - leftFirst) {
                invalidLeaves++;
                continue;
            }

            leafCount++;
            for (uint32_t i = 0u; i < countRight; i++) {
                const uint32_t triangleId = leftFirst + i;
                const RTRCheckTriangle *tri = triangles + triangleId;
                const RTRCheckVec3 verts[3] = {tri->v0, tri->v1, tri->v2};
                float triSlop = 0.0f;

                if (triangleRefs[triangleId]) {
                    duplicateRefs++;
                } else {
                    triangleRefs[triangleId] = 1u;
                    coveredTriangles++;
                }

                for (uint32_t v = 0u; v < 3u; v++) {
                    const float slop =
                        rtrCheckPointBoundsSlop(verts[v], boundsMin, boundsMax);
                    if (slop > triSlop)
                        triSlop = slop;
                }

                if (triSlop > maxTriangleSlop)
                    maxTriangleSlop = triSlop;
                if (triSlop > RTR_CHECK_CONTAINMENT_EPS)
                    outsideTriangles++;
            }
        }
    }

    for (uint32_t i = 0u; i < triangleCount; i++) {
        if (!triangleRefs[i])
            missingTriangles++;
    }

    free(triangleRefs);

    if (invalidBounds || invalidLinks || invalidLeaves ||
        outsideTriangles || outsideChildren || duplicateRefs ||
        missingTriangles) {
        fprintf(stderr,
                "correctness: containment failed invalid_bounds=%u invalid_links=%u invalid_leaves=%u outside_tris=%u outside_children=%u duplicate_refs=%u missing_tris=%u max_tri_slop=%g max_child_slop=%g\n",
                invalidBounds,
                invalidLinks,
                invalidLeaves,
                outsideTriangles,
                outsideChildren,
                duplicateRefs,
                missingTriangles,
                maxTriangleSlop,
                maxChildSlop);
        return 1;
    }

    printf("containment: ok, %u leaves covered %u triangles max_tri_slop %.3g max_child_slop %.3g\n",
           leafCount,
           coveredTriangles,
           maxTriangleSlop,
           maxChildSlop);
    return 0;
}

static int rtrCheckBVH(const uint32_t *words,
                       const RTRCheckTriangle *triangles,
                       uint32_t triangleCount,
                       uint32_t nodeCount,
                       RTRCheckVec3 rootMin,
                       RTRCheckVec3 rootMax)
{
    uint32_t failures = 0u;
    const RTRCheckVec3 center = rtrCheckMul(rtrCheckAdd(rootMin, rootMax), 0.5f);
    const RTRCheckVec3 extent = rtrCheckSub(rootMax, rootMin);
    const float radius = sqrtf(rtrCheckDot(extent, extent)) * 0.9f + 1.0f;

    for (uint32_t i = 0u; i < 96u; i++) {
        const float angle = 6.2831853f * rtrCheckHash01(i * 747796405u + 0x10203040u);
        const float yJitter = rtrCheckHash01(i * 2891336453u + 0x50607080u);
        RTRCheckVec3 ro;
        RTRCheckVec3 target;
        RTRCheckVec3 rd;
        RTRCheckHit brute;
        RTRCheckHit bvh;

        ro.x = center.x + cosf(angle) * radius;
        ro.y = center.y + (yJitter - 0.25f) * radius;
        ro.z = center.z + sinf(angle) * radius;
        target.x = rootMin.x + rtrCheckHash01(i * 2246822519u + 0x90abcdefu) * extent.x;
        target.y = rootMin.y + rtrCheckHash01(i * 3266489917u + 0x13579bdfu) * extent.y;
        target.z = rootMin.z + rtrCheckHash01(i * 668265263u + 0x2468ace0u) * extent.z;
        rd = rtrCheckNormalize(rtrCheckSub(target, ro));

        brute = rtrCheckTraceBrute(ro, rd, triangles, triangleCount);
        bvh = rtrCheckTraceBVH(ro, rd, words, triangles, triangleCount, nodeCount);

        if (bvh.id == -2 ||
            ((brute.id < 0) != (bvh.id < 0)) ||
            (brute.id >= 0 && fabsf(brute.t - bvh.t) > 0.0005f)) {
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
    RTRCheckTriangle *triangles =
        (RTRCheckTriangle *)malloc(sizeof(*triangles) *
                                   (size_t)RTR_CHECK_TRIANGLE_CAPACITY);
    uint32_t triangleCount = 0u;
    uint32_t nodeCount = 0u;
    RTRCheckVec3 rootMin;
    RTRCheckVec3 rootMax;
    int failed = 0;

    if (!(words && triangles)) {
        fprintf(stderr, "correctness: allocation failed\n");
        free(words);
        free(triangles);
        return 1;
    }

    rtrScene(words);
    triangleCount = words[RTR_CHECK_TRIANGLE_COUNT_WORD];
    nodeCount = words[RTR_CHECK_BVH_NODE_COUNT_WORD];

    if (triangleCount == 0u ||
        triangleCount > RTR_CHECK_TRIANGLE_CAPACITY ||
        nodeCount == 0u ||
        nodeCount > RTR_CHECK_BVH_NODE_COUNT) {
        fprintf(stderr, "correctness: invalid counts triangles=%u nodes=%u\n",
                triangleCount, nodeCount);
        free(words);
        free(triangles);
        return 1;
    }

    for (uint32_t i = 0u; i < triangleCount; i++)
        triangles[i] = rtrCheckLoadTriangle(words, i);

    rootMin = rtrCheckLoadBVHMin(words, 0u);
    rootMax = rtrCheckLoadBVHMax(words, 0u);

    failed |= rtrCheckSceneGeometry(triangles, triangleCount, rootMin, rootMax);
    failed |= rtrCheckBVHContainment(words, triangles, triangleCount, nodeCount);
    failed |= rtrCheckBVH(words, triangles, triangleCount, nodeCount, rootMin, rootMax);

    free(words);
    free(triangles);
    return failed ? 1 : 0;
}
