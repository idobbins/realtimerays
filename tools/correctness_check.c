#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RTR_CHECK_HEADER_WORDS 32u
#define RTR_CHECK_BRICK_SIZE 4u
#define RTR_CHECK_BRICK_GRID_X 16u
#define RTR_CHECK_BRICK_GRID_Y 12u
#define RTR_CHECK_BRICK_GRID_Z 16u
#define RTR_CHECK_BRICK_CAPACITY \
    (RTR_CHECK_BRICK_GRID_X * RTR_CHECK_BRICK_GRID_Y * RTR_CHECK_BRICK_GRID_Z)
#define RTR_CHECK_BRICK_WORDS 2u
#define RTR_CHECK_ENVMAP_WORDS (1024u * 512u * 2u)
#define RTR_CHECK_ENVMAP_DIFFUSE_WORDS (32u * 16u * 2u)
#define RTR_CHECK_WORDS \
    (RTR_CHECK_HEADER_WORDS + \
     RTR_CHECK_BRICK_CAPACITY + \
     RTR_CHECK_BRICK_CAPACITY * RTR_CHECK_BRICK_WORDS + \
     RTR_CHECK_ENVMAP_WORDS + \
     RTR_CHECK_ENVMAP_DIFFUSE_WORDS)

#define RTR_CHECK_BRICK_COUNT_WORD 8u
#define RTR_CHECK_BRICK_GRID_X_WORD 9u
#define RTR_CHECK_BRICK_GRID_Y_WORD 10u
#define RTR_CHECK_BRICK_GRID_Z_WORD 11u
#define RTR_CHECK_SCENE_MIN_WORD 24u
#define RTR_CHECK_SCENE_MAX_WORD 27u
#define RTR_CHECK_VOXEL_MAP_WORD 32u
#define RTR_CHECK_VOXEL_BRICK_WORD \
    (RTR_CHECK_VOXEL_MAP_WORD + RTR_CHECK_BRICK_CAPACITY)

void rtrScene(uint32_t *words);

static float rtrCheckLoadF32(const uint32_t *words, uint32_t word)
{
    float value = 0.0f;
    const uint32_t bits = words[word];
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t rtrCheckPopcount(uint32_t value)
{
    uint32_t count = 0u;
    while (value) {
        value &= value - 1u;
        count++;
    }
    return count;
}

int main(void)
{
    uint32_t *words = (uint32_t *)calloc((size_t)RTR_CHECK_WORDS,
                                        sizeof(*words));
    uint8_t *seen = (uint8_t *)calloc((size_t)RTR_CHECK_BRICK_CAPACITY,
                                      sizeof(*seen));
    uint32_t brickCount;
    uint32_t invalidRefs = 0u;
    uint32_t duplicateRefs = 0u;
    uint32_t missingRefs = 0u;
    uint32_t emptyBricks = 0u;
    uint32_t referencedBricks = 0u;
    uint32_t occupiedVoxels = 0u;
    float minX;
    float minY;
    float minZ;
    float maxX;
    float maxY;
    float maxZ;

    if (!(words && seen)) {
        fprintf(stderr, "correctness: allocation failed\n");
        free(words);
        free(seen);
        return 1;
    }

    rtrScene(words);

    brickCount = words[RTR_CHECK_BRICK_COUNT_WORD];
    if (brickCount == 0u || brickCount > RTR_CHECK_BRICK_CAPACITY ||
        words[RTR_CHECK_BRICK_GRID_X_WORD] != RTR_CHECK_BRICK_GRID_X ||
        words[RTR_CHECK_BRICK_GRID_Y_WORD] != RTR_CHECK_BRICK_GRID_Y ||
        words[RTR_CHECK_BRICK_GRID_Z_WORD] != RTR_CHECK_BRICK_GRID_Z) {
        fprintf(stderr,
                "correctness: voxel header failed bricks=%u grid=%u %u %u\n",
                brickCount,
                words[RTR_CHECK_BRICK_GRID_X_WORD],
                words[RTR_CHECK_BRICK_GRID_Y_WORD],
                words[RTR_CHECK_BRICK_GRID_Z_WORD]);
        free(words);
        free(seen);
        return 1;
    }

    for (uint32_t i = 0u; i < RTR_CHECK_BRICK_CAPACITY; i++) {
        const uint32_t ref = words[RTR_CHECK_VOXEL_MAP_WORD + i];
        if (ref == 0u) continue;
        if (ref > brickCount) {
            invalidRefs++;
            continue;
        }
        if (seen[ref - 1u])
            duplicateRefs++;
        seen[ref - 1u] = 1u;
        referencedBricks++;
    }

    for (uint32_t i = 0u; i < brickCount; i++) {
        const uint32_t word = RTR_CHECK_VOXEL_BRICK_WORD +
            i * RTR_CHECK_BRICK_WORDS;
        const uint32_t lo = words[word];
        const uint32_t hi = words[word + 1u];
        if (!seen[i])
            missingRefs++;
        if (!(lo || hi))
            emptyBricks++;
        occupiedVoxels += rtrCheckPopcount(lo) + rtrCheckPopcount(hi);
    }

    minX = rtrCheckLoadF32(words, RTR_CHECK_SCENE_MIN_WORD);
    minY = rtrCheckLoadF32(words, RTR_CHECK_SCENE_MIN_WORD + 1u);
    minZ = rtrCheckLoadF32(words, RTR_CHECK_SCENE_MIN_WORD + 2u);
    maxX = rtrCheckLoadF32(words, RTR_CHECK_SCENE_MAX_WORD);
    maxY = rtrCheckLoadF32(words, RTR_CHECK_SCENE_MAX_WORD + 1u);
    maxZ = rtrCheckLoadF32(words, RTR_CHECK_SCENE_MAX_WORD + 2u);

    if (invalidRefs || duplicateRefs || missingRefs || emptyBricks ||
        !isfinite(minX) || !isfinite(minY) || !isfinite(minZ) ||
        !isfinite(maxX) || !isfinite(maxY) || !isfinite(maxZ) ||
        minX >= maxX || minY >= maxY || minZ >= maxZ) {
        fprintf(stderr,
                "correctness: voxel failed invalid_refs=%u duplicate_refs=%u missing_refs=%u empty_bricks=%u bounds=[%g %g %g]-[%g %g %g]\n",
                invalidRefs,
                duplicateRefs,
                missingRefs,
                emptyBricks,
                minX, minY, minZ,
                maxX, maxY, maxZ);
        free(words);
        free(seen);
        return 1;
    }

    printf("voxels: ok, %u bricks referenced %u occupied_voxels %u bounds [(%.3f %.3f %.3f) (%.3f %.3f %.3f)]\n",
           brickCount,
           referencedBricks,
           occupiedVoxels,
           minX, minY, minZ,
           maxX, maxY, maxZ);

    free(words);
    free(seen);
    return 0;
}
