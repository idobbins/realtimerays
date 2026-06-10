#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RTR_CHECK_HEADER_WORDS 32u
#define RTR_CHECK_BRICK_SIZE 4u
#define RTR_CHECK_BRICK_GRID_X 32u
#define RTR_CHECK_BRICK_GRID_Y 12u
#define RTR_CHECK_BRICK_GRID_Z 32u
#define RTR_CHECK_BRICK_CAPACITY \
    (RTR_CHECK_BRICK_GRID_X * RTR_CHECK_BRICK_GRID_Y * RTR_CHECK_BRICK_GRID_Z)
#define RTR_CHECK_BRICK_WORDS 2u
#define RTR_CHECK_META_WORDS (RTR_CHECK_BRICK_CAPACITY / 2u)
#define RTR_CHECK_DISTANCE_MAX 15u
#define RTR_CHECK_ENVMAP_WORDS (1024u * 512u * 2u)
#define RTR_CHECK_WORDS \
    (RTR_CHECK_HEADER_WORDS + \
     RTR_CHECK_META_WORDS + \
     RTR_CHECK_BRICK_CAPACITY * RTR_CHECK_BRICK_WORDS + \
     RTR_CHECK_ENVMAP_WORDS)

#define RTR_CHECK_BRICK_COUNT_WORD 8u
#define RTR_CHECK_BRICK_GRID_X_WORD 9u
#define RTR_CHECK_BRICK_GRID_Y_WORD 10u
#define RTR_CHECK_BRICK_GRID_Z_WORD 11u
#define RTR_CHECK_SCENE_MIN_WORD 24u
#define RTR_CHECK_SCENE_MAX_WORD 27u
#define RTR_CHECK_META_WORD 32u
#define RTR_CHECK_VOXEL_BRICK_WORD \
    (RTR_CHECK_META_WORD + RTR_CHECK_META_WORDS)

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
    uint32_t *occupied = (uint32_t *)calloc((size_t)RTR_CHECK_BRICK_CAPACITY,
                                            sizeof(*occupied));
    uint32_t brickCount;
    uint32_t occupiedBricks = 0u;
    uint32_t distanceMismatches = 0u;
    uint32_t boundsMismatches = 0u;
    uint32_t occupiedVoxels = 0u;
    uint32_t missingFloorBricks = 0u;
    uint32_t incompleteFloorBricks = 0u;
    float minX;
    float minY;
    float minZ;
    float maxX;
    float maxY;
    float maxZ;

    if (!(words && occupied)) {
        fprintf(stderr, "correctness: allocation failed\n");
        free(words);
        free(occupied);
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
        free(occupied);
        return 1;
    }

    for (uint32_t i = 0u; i < RTR_CHECK_BRICK_CAPACITY; i++) {
        const uint32_t word = RTR_CHECK_VOXEL_BRICK_WORD +
            i * RTR_CHECK_BRICK_WORDS;
        const uint32_t lo = words[word];
        const uint32_t hi = words[word + 1u];
        const uint32_t stored =
            ((words[RTR_CHECK_META_WORD + i / 2u] >> ((i % 2u) * 16u)) >> 4u) &
            0xfffu;
        uint32_t boundsMin[3] = {3u, 3u, 3u};
        uint32_t boundsMax[3] = {0u, 0u, 0u};
        uint32_t reference = 0u;

        occupied[i] = (lo || hi) ? 1u : 0u;
        occupiedBricks += occupied[i];
        occupiedVoxels += rtrCheckPopcount(lo) + rtrCheckPopcount(hi);

        for (uint32_t bit = 0u; bit < 64u; bit++) {
            const uint32_t set = bit < 32u ?
                (lo >> bit) & 1u : (hi >> (bit - 32u)) & 1u;
            const uint32_t lx = bit & 3u;
            const uint32_t lz = (bit >> 2u) & 3u;
            const uint32_t ly = bit >> 4u;

            if (!set) continue;
            if (lx < boundsMin[0]) boundsMin[0] = lx;
            if (ly < boundsMin[1]) boundsMin[1] = ly;
            if (lz < boundsMin[2]) boundsMin[2] = lz;
            if (lx > boundsMax[0]) boundsMax[0] = lx;
            if (ly > boundsMax[1]) boundsMax[1] = ly;
            if (lz > boundsMax[2]) boundsMax[2] = lz;
        }

        if (occupied[i]) {
            reference = boundsMin[0] | (boundsMin[1] << 2u) |
                (boundsMin[2] << 4u) | (boundsMax[0] << 6u) |
                (boundsMax[1] << 8u) | (boundsMax[2] << 10u);
        }
        if (stored != reference)
            boundsMismatches++;
    }

    /* Brute-force Chebyshev reference for the stored distance field. */
    for (uint32_t by = 0u; by < RTR_CHECK_BRICK_GRID_Y; by++) {
        for (uint32_t bz = 0u; bz < RTR_CHECK_BRICK_GRID_Z; bz++) {
            for (uint32_t bx = 0u; bx < RTR_CHECK_BRICK_GRID_X; bx++) {
                const uint32_t i =
                    (by * RTR_CHECK_BRICK_GRID_Z + bz) *
                    RTR_CHECK_BRICK_GRID_X + bx;
                const uint32_t stored =
                    (words[RTR_CHECK_META_WORD + i / 2u] >>
                     ((i % 2u) * 16u)) & 0xfu;
                uint32_t reference = RTR_CHECK_DISTANCE_MAX;

                for (uint32_t qy = 0u; qy < RTR_CHECK_BRICK_GRID_Y; qy++) {
                    for (uint32_t qz = 0u; qz < RTR_CHECK_BRICK_GRID_Z; qz++) {
                        for (uint32_t qx = 0u;
                             qx < RTR_CHECK_BRICK_GRID_X;
                             qx++) {
                            const uint32_t q =
                                (qy * RTR_CHECK_BRICK_GRID_Z + qz) *
                                RTR_CHECK_BRICK_GRID_X + qx;
                            uint32_t dx;
                            uint32_t dy;
                            uint32_t dz;
                            uint32_t d;

                            if (!occupied[q]) continue;
                            dx = qx > bx ? qx - bx : bx - qx;
                            dy = qy > by ? qy - by : by - qy;
                            dz = qz > bz ? qz - bz : bz - qz;
                            d = dx > dy ? dx : dy;
                            if (dz > d) d = dz;
                            if (d < reference) reference = d;
                        }
                    }
                }

                if (stored != reference)
                    distanceMismatches++;
            }
        }
    }

    for (uint32_t bz = 0u; bz < RTR_CHECK_BRICK_GRID_Z; bz++) {
        for (uint32_t bx = 0u; bx < RTR_CHECK_BRICK_GRID_X; bx++) {
            const uint32_t brickIndex = bz * RTR_CHECK_BRICK_GRID_X + bx;
            uint32_t lo;
            if (!occupied[brickIndex]) {
                missingFloorBricks++;
                continue;
            }
            lo = words[RTR_CHECK_VOXEL_BRICK_WORD +
                       brickIndex * RTR_CHECK_BRICK_WORDS];
            if ((lo & 0xffffu) != 0xffffu)
                incompleteFloorBricks++;
        }
    }

    minX = rtrCheckLoadF32(words, RTR_CHECK_SCENE_MIN_WORD);
    minY = rtrCheckLoadF32(words, RTR_CHECK_SCENE_MIN_WORD + 1u);
    minZ = rtrCheckLoadF32(words, RTR_CHECK_SCENE_MIN_WORD + 2u);
    maxX = rtrCheckLoadF32(words, RTR_CHECK_SCENE_MAX_WORD);
    maxY = rtrCheckLoadF32(words, RTR_CHECK_SCENE_MAX_WORD + 1u);
    maxZ = rtrCheckLoadF32(words, RTR_CHECK_SCENE_MAX_WORD + 2u);

    if (distanceMismatches || boundsMismatches ||
        occupiedBricks != brickCount ||
        missingFloorBricks || incompleteFloorBricks ||
        !isfinite(minX) || !isfinite(minY) || !isfinite(minZ) ||
        !isfinite(maxX) || !isfinite(maxY) || !isfinite(maxZ) ||
        minX >= maxX || minY >= maxY || minZ >= maxZ) {
        fprintf(stderr,
                "correctness: voxel failed distance_mismatches=%u bounds_mismatches=%u occupied=%u count=%u missing_floor=%u incomplete_floor=%u bounds=[%g %g %g]-[%g %g %g]\n",
                distanceMismatches,
                boundsMismatches,
                occupiedBricks,
                brickCount,
                missingFloorBricks,
                incompleteFloorBricks,
                minX, minY, minZ,
                maxX, maxY, maxZ);
        free(words);
        free(occupied);
        return 1;
    }

    printf("voxels: ok, %u bricks occupied_voxels %u floor_bricks %u bounds [(%.3f %.3f %.3f) (%.3f %.3f %.3f)]\n",
           brickCount,
           occupiedVoxels,
           RTR_CHECK_BRICK_GRID_X * RTR_CHECK_BRICK_GRID_Z,
           minX, minY, minZ,
           maxX, maxY, maxZ);

    free(words);
    free(occupied);
    return 0;
}
