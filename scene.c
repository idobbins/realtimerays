#include <stdint.h>
#include <string.h>
#include <stdlib.h>

enum {
    RTR_SCENE_COUNT_WORD = 8,
    RTR_SCENE_BOUNDS_MIN_WORD = 9,
    RTR_SCENE_BOUNDS_MAX_WORD = 12,
    RTR_SCENE_GEOM_WORD = 16,
    RTR_SCENE_SPHERE_COUNT = 100,
    RTR_SCENE_SPHERE_WORDS = 4,
    RTR_SCENE_MAT_WORD = RTR_SCENE_GEOM_WORD + RTR_SCENE_SPHERE_COUNT * RTR_SCENE_SPHERE_WORDS,
};

typedef struct RTRSphere {
    float geom[4];
    float mat[4];
    uint32_t morton;
} RTRSphere;

static uint32_t rtrMorton2D(uint32_t x, uint32_t y)
{
    uint32_t code = 0u;

    for (uint32_t bit = 0u; bit < 16u; bit++) {
        code |= ((x >> bit) & 1u) << (bit * 2u);
        code |= ((y >> bit) & 1u) << (bit * 2u + 1u);
    }

    return code;
}

static int rtrCompareSphere(const void *lhs, const void *rhs)
{
    const RTRSphere *a = (const RTRSphere *)lhs;
    const RTRSphere *b = (const RTRSphere *)rhs;

    if (a->geom[3] > b->geom[3]) return -1;
    if (a->geom[3] < b->geom[3]) return 1;
    if (a->morton < b->morton) return -1;
    if (a->morton > b->morton) return 1;
    return 0;
}

void rtrScene(uint32_t *words)
{
    words[RTR_SCENE_COUNT_WORD] = RTR_SCENE_SPHERE_COUNT;

    RTRSphere spheres[RTR_SCENE_SPHERE_COUNT];
    float boundsMin[3] = {1.0e20f, 1.0e20f, 1.0e20f};
    float boundsMax[3] = {-1.0e20f, -1.0e20f, -1.0e20f};

    for (uint32_t z = 0u; z < 10u; z++) {
        for (uint32_t x = 0u; x < 10u; x++) {
            const uint32_t i = z * 10u + x;
            const float fx = ((float)x - 4.5f) * 0.42f;
            const float fz = ((float)z - 4.5f) * 0.42f;
            const float r = 0.10f + (float)((x * 3u + z * 5u) % 5u) * 0.025f;
            const float u = (float)x / 9.0f;
            const float v = (float)z / 9.0f;
            const float w = (float)((x + z) % 10u) / 9.0f;

            spheres[i] = (RTRSphere){
                .geom = {fx, -1.0f + r, fz, r},
                .mat = {
                    0.24f + 0.56f * u,
                    0.25f + 0.38f * w,
                    0.34f + 0.42f * v,
                    1.0f / r,
                },
                .morton = rtrMorton2D(x, z),
            };

            const float cy = -1.0f + r;
            const float lo[3] = {fx - r, cy - r, fz - r};
            const float hi[3] = {fx + r, cy + r, fz + r};
            for (uint32_t axis = 0u; axis < 3u; axis++) {
                if (lo[axis] < boundsMin[axis]) boundsMin[axis] = lo[axis];
                if (hi[axis] > boundsMax[axis]) boundsMax[axis] = hi[axis];
            }
        }
    }

    qsort(spheres, RTR_SCENE_SPHERE_COUNT, sizeof(spheres[0]), rtrCompareSphere);

    memcpy(words + RTR_SCENE_BOUNDS_MIN_WORD, boundsMin, sizeof(boundsMin));
    memcpy(words + RTR_SCENE_BOUNDS_MAX_WORD, boundsMax, sizeof(boundsMax));

    for (uint32_t i = 0u; i < RTR_SCENE_SPHERE_COUNT; i++) {
        memcpy(words + RTR_SCENE_GEOM_WORD + i * RTR_SCENE_SPHERE_WORDS,
               spheres[i].geom, sizeof(spheres[i].geom));
        memcpy(words + RTR_SCENE_MAT_WORD + i * RTR_SCENE_SPHERE_WORDS,
               spheres[i].mat, sizeof(spheres[i].mat));
    }
}
