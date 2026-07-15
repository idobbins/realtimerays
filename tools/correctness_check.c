#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../scene_layout.h"

#define RTR_CHECK_DISTANCE_MAX 15u
#define RTR_CHECK_GUARD_WORDS 32u
#define RTR_CHECK_FOREST_HASH UINT64_C(0x6578325ae75c4518)
#define RTR_CHECK_CASTLE_HASH UINT64_C(0xa313ba0b00a8456a)
#define RTR_CHECK_CITY100K_HASH UINT64_C(0x1b2be050a17376c1)
#define RTR_CHECK_WORLD_HASH UINT64_C(0x77c399716ad60cb7)

#if RTR_LAYOUT_SCENE_KIND_WORD != 31u
#error "scene kind collides with live configuration or profiling header words"
#endif

void rtrSceneBuild(uint32_t *words, uint32_t sceneKind);
uint32_t rtrSceneKindFromName(const char *scene);

static float rtrCheckLoadF32(const uint32_t *words, uint32_t word)
{
    float value = 0.0f;
    memcpy(&value, &words[word], sizeof(value));
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

static uint32_t rtrCheckBrickIndex(uint32_t bx, uint32_t by, uint32_t bz)
{
    return (by * RTR_LAYOUT_BRICK_GRID_Z + bz) *
        RTR_LAYOUT_BRICK_GRID_X + bx;
}

static uint32_t rtrCheckVoxelBit(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & 3u) + (z & 3u) * 4u + (y & 3u) * 16u;
}

static uint32_t rtrCheckOccupiedAt(const uint32_t *words,
                                   uint32_t x,
                                   uint32_t y,
                                   uint32_t z)
{
    const uint32_t brick = rtrCheckBrickIndex(x / 4u, y / 4u, z / 4u);
    const uint32_t bit = rtrCheckVoxelBit(x, y, z);
    const uint32_t word = RTR_LAYOUT_VOXEL_BRICK_WORD +
        brick * RTR_LAYOUT_BRICK_WORDS + bit / 32u;

    return (words[word] >> (bit & 31u)) & 1u;
}

static uint32_t rtrCheckMaterialAt(const uint32_t *words,
                                   uint32_t x,
                                   uint32_t y,
                                   uint32_t z)
{
    const uint32_t brick = rtrCheckBrickIndex(x / 4u, y / 4u, z / 4u);
    const uint32_t bit = rtrCheckVoxelBit(x, y, z);
    const uint32_t word = RTR_LAYOUT_MATERIAL_WORD +
        brick * RTR_LAYOUT_MATERIAL_WORDS_PER_BRICK + bit / 16u;

    return (words[word] >> ((bit & 15u) * 2u)) & 3u;
}

static uint64_t rtrCheckGeometryHash(const uint32_t *words)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    const uint32_t end = RTR_LAYOUT_MATERIAL_WORD + RTR_LAYOUT_MATERIAL_WORDS;

    for (uint32_t i = RTR_LAYOUT_META_WORD; i < end; i++) {
        hash ^= words[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int rtrCheckHitPacking(void)
{
    static const uint32_t coordinates[] = {0u, 127u, 128u, 191u, 255u};

    for (uint32_t axis = 0u; axis < 3u; axis++) {
        for (uint32_t i = 0u;
             i < sizeof(coordinates) / sizeof(coordinates[0]);
             i++) {
            const uint32_t x = coordinates[i];
            const uint32_t y = coordinates[(i + 1u) % 5u];
            const uint32_t z = coordinates[(i + 2u) % 5u];
            const uint32_t word = 0x80000000u | (axis << 24u) |
                x | (y << 8u) | (z << 16u);

            if ((word & 255u) != x || ((word >> 8u) & 255u) != y ||
                ((word >> 16u) & 255u) != z ||
                ((word >> 24u) & 3u) != axis) {
                fprintf(stderr, "correctness: hit packing failed\n");
                return 1;
            }
        }
    }
    return 0;
}

static int rtrCheckSceneNames(void)
{
    if (rtrSceneKindFromName(NULL) != RTR_SCENE_KIND_FOREST ||
        rtrSceneKindFromName("forest") != RTR_SCENE_KIND_FOREST ||
        rtrSceneKindFromName("unknown") != RTR_SCENE_KIND_FOREST ||
        rtrSceneKindFromName("castle") != RTR_SCENE_KIND_CASTLE ||
        rtrSceneKindFromName("city100k") != RTR_SCENE_KIND_CITY100K ||
        rtrSceneKindFromName("100k") != RTR_SCENE_KIND_CITY100K ||
        rtrSceneKindFromName("world") != RTR_SCENE_KIND_WORLD ||
        rtrSceneKindFromName("minecraft") != RTR_SCENE_KIND_WORLD) {
        fprintf(stderr, "correctness: scene name parsing failed\n");
        return 1;
    }
    return 0;
}

static int rtrCheckScene(uint32_t sceneKind,
                         const char *name,
                         uint64_t *geometryHash)
{
    const size_t wordCount =
        (size_t)RTR_LAYOUT_WF_WORD + RTR_CHECK_GUARD_WORDS;
    uint32_t *words = (uint32_t *)malloc(wordCount * sizeof(*words));
    uint8_t *occupied =
        (uint8_t *)calloc((size_t)RTR_LAYOUT_BRICK_CAPACITY, 1u);
    uint8_t *referenceDistance =
        (uint8_t *)malloc((size_t)RTR_LAYOUT_BRICK_CAPACITY);
    uint32_t *distanceQueue =
        (uint32_t *)malloc((size_t)RTR_LAYOUT_BRICK_CAPACITY *
                           sizeof(*distanceQueue));
    uint32_t brickCount;
    uint32_t occupiedBricks = 0u;
    uint32_t occupiedVoxels = 0u;
    uint32_t distanceMismatches = 0u;
    uint32_t boundsMismatches = 0u;
    uint32_t materialOnEmpty = 0u;
    uint32_t missingFloorBricks = 0u;
    uint32_t incompleteFloorBricks = 0u;
    uint32_t unexpectedFloorVoxels = 0u;
    uint32_t badFloorMaterials = 0u;
    uint32_t exposedRootVoxels = 0u;
    uint32_t embeddedStoneVoxels = 0u;
    uint32_t materialCounts[4] = {0u, 0u, 0u, 0u};
    uint32_t maxOccupiedY = 0u;
    uint32_t forestRadiusViolations = 0u;
    uint32_t isolatedFoliage = 0u;
    int failed = 0;

    if (!(words && occupied && referenceDistance && distanceQueue)) {
        fprintf(stderr, "correctness: allocation failed\n");
        free(words);
        free(occupied);
        free(referenceDistance);
        free(distanceQueue);
        return 1;
    }

    memset(referenceDistance, 255, (size_t)RTR_LAYOUT_BRICK_CAPACITY);

    for (size_t i = 0u; i < wordCount; i++) words[i] = 0xa5a5a5a5u;
    for (uint32_t i = 0u; i < RTR_CHECK_GUARD_WORDS; i++)
        words[RTR_LAYOUT_WF_WORD + i] = 0xdeadbeefu;
    rtrSceneBuild(words, sceneKind);

    for (uint32_t i = 0u; i < RTR_CHECK_GUARD_WORDS; i++) {
        if (words[RTR_LAYOUT_WF_WORD + i] != 0xdeadbeefu) {
            fprintf(stderr, "correctness: %s scene overran scene storage\n",
                    name);
            failed = 1;
            break;
        }
    }

    brickCount = words[8u];
    if (brickCount == 0u || brickCount > RTR_LAYOUT_BRICK_CAPACITY ||
        words[9u] != RTR_LAYOUT_BRICK_GRID_X ||
        words[10u] != RTR_LAYOUT_BRICK_GRID_Y ||
        words[11u] != RTR_LAYOUT_BRICK_GRID_Z ||
        words[RTR_LAYOUT_SCENE_KIND_WORD] != sceneKind) {
        fprintf(stderr,
                "correctness: %s header failed bricks=%u grid=%u %u %u kind=%u\n",
                name, brickCount, words[9u], words[10u], words[11u],
                words[RTR_LAYOUT_SCENE_KIND_WORD]);
        failed = 1;
    }

    for (uint32_t by = 0u; by < RTR_LAYOUT_BRICK_GRID_Y; by++) {
        for (uint32_t bz = 0u; bz < RTR_LAYOUT_BRICK_GRID_Z; bz++) {
            for (uint32_t bx = 0u; bx < RTR_LAYOUT_BRICK_GRID_X; bx++) {
                const uint32_t brick = rtrCheckBrickIndex(bx, by, bz);
                const uint32_t maskWord = RTR_LAYOUT_VOXEL_BRICK_WORD +
                    brick * RTR_LAYOUT_BRICK_WORDS;
                const uint32_t lo = words[maskWord];
                const uint32_t hi = words[maskWord + 1u];
                const uint32_t storedBounds =
                    ((words[RTR_LAYOUT_META_WORD + brick / 2u] >>
                      ((brick & 1u) * 16u)) >> 4u) & 0xfffu;
                uint32_t boundsMin[3] = {3u, 3u, 3u};
                uint32_t boundsMax[3] = {0u, 0u, 0u};
                uint32_t referenceBounds = 0u;

                occupied[brick] = (lo || hi) ? 1u : 0u;
                if (occupied[brick]) {
                    referenceDistance[brick] = 0u;
                    distanceQueue[occupiedBricks++] = brick;
                }
                occupiedVoxels += rtrCheckPopcount(lo) + rtrCheckPopcount(hi);

                for (uint32_t bit = 0u; bit < 64u; bit++) {
                    const uint32_t set = bit < 32u ?
                        (lo >> bit) & 1u : (hi >> (bit - 32u)) & 1u;
                    const uint32_t lx = bit & 3u;
                    const uint32_t lz = (bit >> 2u) & 3u;
                    const uint32_t ly = bit >> 4u;
                    const uint32_t x = bx * 4u + lx;
                    const uint32_t y = by * 4u + ly;
                    const uint32_t z = bz * 4u + lz;
                    const uint32_t material =
                        rtrCheckMaterialAt(words, x, y, z);

                    if (!set) {
                        if (material != 0u) materialOnEmpty++;
                        continue;
                    }
                    materialCounts[material]++;
                    if (y > maxOccupiedY) maxOccupiedY = y;
                    if (sceneKind == RTR_SCENE_KIND_FOREST && y > 0u &&
                        (material == RTR_MATERIAL_WOOD ||
                         material == RTR_MATERIAL_FOLIAGE)) {
                        const int32_t dx = (int32_t)x -
                            (int32_t)(RTR_LAYOUT_VOXEL_GRID_X / 2u);
                        const int32_t dz = (int32_t)z -
                            (int32_t)(RTR_LAYOUT_VOXEL_GRID_Z / 2u);
                        if (dx * dx + dz * dz > 52 * 52)
                            forestRadiusViolations++;
                    }
                    if (lx < boundsMin[0]) boundsMin[0] = lx;
                    if (ly < boundsMin[1]) boundsMin[1] = ly;
                    if (lz < boundsMin[2]) boundsMin[2] = lz;
                    if (lx > boundsMax[0]) boundsMax[0] = lx;
                    if (ly > boundsMax[1]) boundsMax[1] = ly;
                    if (lz > boundsMax[2]) boundsMax[2] = lz;
                }

                if (occupied[brick]) {
                    referenceBounds =
                        boundsMin[0] | (boundsMin[1] << 2u) |
                        (boundsMin[2] << 4u) | (boundsMax[0] << 6u) |
                        (boundsMax[1] << 8u) | (boundsMax[2] << 10u);
                }
                if (storedBounds != referenceBounds) boundsMismatches++;
            }
        }
    }

    {
        uint32_t queueHead = 0u;
        uint32_t queueTail = occupiedBricks;

        while (queueHead < queueTail) {
            const uint32_t brick = distanceQueue[queueHead++];
            const uint32_t bx = brick % RTR_LAYOUT_BRICK_GRID_X;
            const uint32_t yz = brick / RTR_LAYOUT_BRICK_GRID_X;
            const uint32_t bz = yz % RTR_LAYOUT_BRICK_GRID_Z;
            const uint32_t by = yz / RTR_LAYOUT_BRICK_GRID_Z;
            const uint8_t next = (uint8_t)(referenceDistance[brick] + 1u);

            for (int32_t dy = -1; dy <= 1; dy++) {
                for (int32_t dz = -1; dz <= 1; dz++) {
                    for (int32_t dx = -1; dx <= 1; dx++) {
                        const int32_t nx = (int32_t)bx + dx;
                        const int32_t ny = (int32_t)by + dy;
                        const int32_t nz = (int32_t)bz + dz;
                        uint32_t neighbor;

                        if ((dx == 0 && dy == 0 && dz == 0) ||
                            nx < 0 || ny < 0 || nz < 0 ||
                            nx >= (int32_t)RTR_LAYOUT_BRICK_GRID_X ||
                            ny >= (int32_t)RTR_LAYOUT_BRICK_GRID_Y ||
                            nz >= (int32_t)RTR_LAYOUT_BRICK_GRID_Z)
                            continue;
                        neighbor = rtrCheckBrickIndex((uint32_t)nx,
                                                      (uint32_t)ny,
                                                      (uint32_t)nz);
                        if (referenceDistance[neighbor] <= next) continue;
                        referenceDistance[neighbor] = next;
                        distanceQueue[queueTail++] = neighbor;
                    }
                }
            }
        }
    }

    for (uint32_t by = 0u; by < RTR_LAYOUT_BRICK_GRID_Y; by++) {
        for (uint32_t bz = 0u; bz < RTR_LAYOUT_BRICK_GRID_Z; bz++) {
            for (uint32_t bx = 0u; bx < RTR_LAYOUT_BRICK_GRID_X; bx++) {
                const uint32_t brick = rtrCheckBrickIndex(bx, by, bz);
                const uint32_t stored =
                    (words[RTR_LAYOUT_META_WORD + brick / 2u] >>
                     ((brick & 1u) * 16u)) & 0xfu;
                const uint32_t reference =
                    referenceDistance[brick] > RTR_CHECK_DISTANCE_MAX ?
                    RTR_CHECK_DISTANCE_MAX : referenceDistance[brick];
                if (stored != reference) distanceMismatches++;
            }
        }
    }

    const uint32_t floorSize = sceneKind == RTR_SCENE_KIND_CASTLE ? 128u :
        (sceneKind == RTR_SCENE_KIND_WORLD ?
         RTR_LAYOUT_VOXEL_GRID_X : 160u);
    const uint32_t floorMinBx =
        (RTR_LAYOUT_VOXEL_GRID_X - floorSize) /
        (2u * RTR_LAYOUT_BRICK_SIZE);
    const uint32_t floorMinBz =
        (RTR_LAYOUT_VOXEL_GRID_Z - floorSize) /
        (2u * RTR_LAYOUT_BRICK_SIZE);
    const uint32_t floorMaxBx =
        floorMinBx + floorSize / RTR_LAYOUT_BRICK_SIZE;
    const uint32_t floorMaxBz =
        floorMinBz + floorSize / RTR_LAYOUT_BRICK_SIZE;

    for (uint32_t bz = 0u; bz < RTR_LAYOUT_BRICK_GRID_Z; bz++) {
        for (uint32_t bx = 0u; bx < RTR_LAYOUT_BRICK_GRID_X; bx++) {
            const uint32_t brick = rtrCheckBrickIndex(bx, 0u, bz);
            const uint32_t lo = words[RTR_LAYOUT_VOXEL_BRICK_WORD +
                                      brick * RTR_LAYOUT_BRICK_WORDS];
            const uint32_t floorMask = lo & 0xffffu;
            const uint32_t expectsFloor =
                bx >= floorMinBx && bx < floorMaxBx &&
                bz >= floorMinBz && bz < floorMaxBz;

            if (!expectsFloor) {
                unexpectedFloorVoxels += rtrCheckPopcount(floorMask);
                continue;
            }
            if (!occupied[brick])
                missingFloorBricks++;
            else if (floorMask != 0xffffu)
                incompleteFloorBricks++;
            for (uint32_t bit = 0u; bit < 16u; bit++) {
                const uint32_t x = bx * RTR_LAYOUT_BRICK_SIZE + (bit & 3u);
                const uint32_t z = bz * RTR_LAYOUT_BRICK_SIZE +
                    ((bit >> 2u) & 3u);
                if ((floorMask >> bit) & 1u) {
                    const uint32_t material =
                        rtrCheckMaterialAt(words, x, 0u, z);
                    if (sceneKind == RTR_SCENE_KIND_FOREST &&
                        material == RTR_MATERIAL_WOOD) {
                        exposedRootVoxels++;
                    } else if (sceneKind == RTR_SCENE_KIND_FOREST &&
                               material == RTR_MATERIAL_STONE) {
                        embeddedStoneVoxels++;
                    } else if (sceneKind == RTR_SCENE_KIND_WORLD &&
                               material != RTR_MATERIAL_STONE) {
                        badFloorMaterials++;
                    } else if (sceneKind != RTR_SCENE_KIND_WORLD &&
                               material != RTR_MATERIAL_GROUND) {
                        badFloorMaterials++;
                    }
                }
            }
        }
    }

    if (sceneKind == RTR_SCENE_KIND_FOREST ||
        sceneKind == RTR_SCENE_KIND_WORLD) {
        static const int32_t neighborOffsets[6][3] = {
            {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
            {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
        };

        for (uint32_t y = 1u; y + 1u < RTR_LAYOUT_VOXEL_GRID_Y; y++) {
            for (uint32_t z = 1u; z + 1u < RTR_LAYOUT_VOXEL_GRID_Z; z++) {
                for (uint32_t x = 1u; x + 1u < RTR_LAYOUT_VOXEL_GRID_X; x++) {
                    uint32_t neighbors = 0u;

                    if (!rtrCheckOccupiedAt(words, x, y, z) ||
                        rtrCheckMaterialAt(words, x, y, z) !=
                            RTR_MATERIAL_FOLIAGE) {
                        continue;
                    }
                    for (uint32_t i = 0u; i < 6u; i++) {
                        const uint32_t nx =
                            (uint32_t)((int32_t)x + neighborOffsets[i][0]);
                        const uint32_t ny =
                            (uint32_t)((int32_t)y + neighborOffsets[i][1]);
                        const uint32_t nz =
                            (uint32_t)((int32_t)z + neighborOffsets[i][2]);
                        if (rtrCheckOccupiedAt(words, nx, ny, nz)) {
                            const uint32_t material =
                                rtrCheckMaterialAt(words, nx, ny, nz);
                            neighbors += material == RTR_MATERIAL_FOLIAGE ||
                                         material == RTR_MATERIAL_WOOD;
                        }
                    }
                    if (neighbors < 2u) isolatedFoliage++;
                }
            }
        }
    }

    {
        const float minX = rtrCheckLoadF32(words, 24u);
        const float minY = rtrCheckLoadF32(words, 25u);
        const float minZ = rtrCheckLoadF32(words, 26u);
        const float maxX = rtrCheckLoadF32(words, 27u);
        const float maxY = rtrCheckLoadF32(words, 28u);
        const float maxZ = rtrCheckLoadF32(words, 29u);

        const float halfX =
            (float)RTR_LAYOUT_VOXEL_GRID_X * 0.055f * 0.5f;
        const float halfZ =
            (float)RTR_LAYOUT_VOXEL_GRID_Z * 0.055f * 0.5f;
        const float topY =
            -1.0f + (float)RTR_LAYOUT_VOXEL_GRID_Y * 0.055f;

        if (!isfinite(minX) || !isfinite(minY) || !isfinite(minZ) ||
            !isfinite(maxX) || !isfinite(maxY) || !isfinite(maxZ) ||
            fabsf(minX + halfX) > 0.001f ||
            fabsf(minY + 1.0f) > 0.001f ||
            fabsf(minZ + halfZ) > 0.001f ||
            fabsf(maxX - halfX) > 0.001f ||
            fabsf(maxY - topY) > 0.001f ||
            fabsf(maxZ - halfZ) > 0.001f) {
            fprintf(stderr, "correctness: %s bounds failed\n", name);
            failed = 1;
        }
    }

    if (distanceMismatches || boundsMismatches || materialOnEmpty ||
        occupiedBricks != brickCount || missingFloorBricks ||
        incompleteFloorBricks || unexpectedFloorVoxels ||
        badFloorMaterials) {
        fprintf(stderr,
                "correctness: %s layout failed dist=%u bounds=%u material_empty=%u occupied=%u count=%u floor=%u/%u unexpected=%u material=%u\n",
                name, distanceMismatches, boundsMismatches, materialOnEmpty,
                occupiedBricks, brickCount, missingFloorBricks,
                incompleteFloorBricks, unexpectedFloorVoxels,
                badFloorMaterials);
        failed = 1;
    }

    if (sceneKind == RTR_SCENE_KIND_CASTLE) {
        if (brickCount != 1507u || occupiedVoxels != 35561u ||
            materialCounts[RTR_MATERIAL_GROUND] != 16384u ||
            materialCounts[RTR_MATERIAL_STONE] != 19177u ||
            materialCounts[RTR_MATERIAL_WOOD] != 0u ||
            materialCounts[RTR_MATERIAL_FOLIAGE] != 0u ||
            maxOccupiedY != 31u) {
            fprintf(stderr,
                    "correctness: castle regression bricks=%u voxels=%u max_y=%u mats=%u/%u/%u/%u\n",
                    brickCount, occupiedVoxels, maxOccupiedY,
                    materialCounts[0], materialCounts[1],
                    materialCounts[2], materialCounts[3]);
            failed = 1;
        }
    } else if (sceneKind == RTR_SCENE_KIND_CITY100K) {
        if (brickCount != 5064u || occupiedVoxels != 100000u ||
            materialCounts[RTR_MATERIAL_GROUND] != 25600u ||
            materialCounts[RTR_MATERIAL_WOOD] != 6168u ||
            materialCounts[RTR_MATERIAL_FOLIAGE] != 0u ||
            materialCounts[RTR_MATERIAL_STONE] != 68232u ||
            maxOccupiedY != 63u) {
            fprintf(stderr,
                    "correctness: city100k regression bricks=%u voxels=%u max_y=%u mats=%u/%u/%u/%u\n",
                    brickCount, occupiedVoxels, maxOccupiedY,
                    materialCounts[0], materialCounts[1],
                    materialCounts[2], materialCounts[3]);
            failed = 1;
        }
    } else if (sceneKind == RTR_SCENE_KIND_FOREST) {
        if (brickCount < 2300u || brickCount > 3000u ||
            occupiedVoxels < 40000u || occupiedVoxels > 65000u ||
            maxOccupiedY < 64u || maxOccupiedY > 74u ||
            materialCounts[0] == 0u || materialCounts[1] == 0u ||
            materialCounts[2] == 0u || materialCounts[3] == 0u ||
            exposedRootVoxels < 32u || exposedRootVoxels > 256u ||
            embeddedStoneVoxels > 64u ||
            forestRadiusViolations || isolatedFoliage) {
            fprintf(stderr,
                    "correctness: forest design bricks=%u voxels=%u max_y=%u mats=%u/%u/%u/%u roots=%u stone_floor=%u radius=%u isolated=%u\n",
                    brickCount, occupiedVoxels, maxOccupiedY,
                    materialCounts[0], materialCounts[1],
                    materialCounts[2], materialCounts[3],
                    exposedRootVoxels, embeddedStoneVoxels,
                    forestRadiusViolations,
                    isolatedFoliage);
            failed = 1;
        }
    } else if (sceneKind == RTR_SCENE_KIND_WORLD) {
        if (brickCount != 17821u || occupiedVoxels != 1029425u ||
            maxOccupiedY != 41u ||
            materialCounts[RTR_MATERIAL_GROUND] != 140544u ||
            materialCounts[RTR_MATERIAL_WOOD] != 318u ||
            materialCounts[RTR_MATERIAL_FOLIAGE] != 3700u ||
            materialCounts[RTR_MATERIAL_STONE] != 884863u ||
            isolatedFoliage) {
            fprintf(stderr,
                    "correctness: world design bricks=%u voxels=%u max_y=%u mats=%u/%u/%u/%u isolated=%u\n",
                    brickCount, occupiedVoxels, maxOccupiedY,
                    materialCounts[0], materialCounts[1],
                    materialCounts[2], materialCounts[3],
                    isolatedFoliage);
            failed = 1;
        }
    } else {
        fprintf(stderr, "correctness: unknown scene kind %u\n", sceneKind);
        failed = 1;
    }

    *geometryHash = rtrCheckGeometryHash(words);
    printf("%s: %s, %u bricks %u voxels max_y %u materials %u/%u/%u/%u hash %016llx\n",
           name, failed ? "FAILED" : "ok", brickCount, occupiedVoxels,
           maxOccupiedY, materialCounts[0], materialCounts[1],
           materialCounts[2], materialCounts[3],
           (unsigned long long)*geometryHash);

    free(words);
    free(occupied);
    free(referenceDistance);
    free(distanceQueue);
    return failed;
}

static int rtrCheckSceneDeterminism(uint32_t sceneKind,
                                    const char *name,
                                    uint64_t referenceHash)
{
    uint32_t *words = (uint32_t *)calloc((size_t)RTR_LAYOUT_WF_WORD,
                                        sizeof(*words));
    uint64_t hash;

    if (!words) return 1;
    rtrSceneBuild(words, sceneKind);
    hash = rtrCheckGeometryHash(words);
    free(words);
    if (hash != referenceHash) {
        fprintf(stderr, "correctness: %s generation is not deterministic\n",
                name);
        return 1;
    }
    return 0;
}

static int rtrCheckGeometryRegression(const char *name,
                                      uint64_t actual,
                                      uint64_t expected)
{
    if (actual == expected) return 0;
    fprintf(stderr,
            "correctness: %s geometry hash changed: %016llx expected %016llx\n",
            name, (unsigned long long)actual,
            (unsigned long long)expected);
    return 1;
}

int main(void)
{
    uint64_t forestHash = 0u;
    uint64_t castleHash = 0u;
    uint64_t city100kHash = 0u;
    uint64_t worldHash = 0u;
    int failed = 0;

    failed |= rtrCheckHitPacking();
    failed |= rtrCheckSceneNames();
    failed |= rtrCheckScene(RTR_SCENE_KIND_FOREST, "forest", &forestHash);
    failed |= rtrCheckSceneDeterminism(RTR_SCENE_KIND_FOREST,
                                       "forest", forestHash);
    failed |= rtrCheckGeometryRegression("forest", forestHash,
                                         RTR_CHECK_FOREST_HASH);
    failed |= rtrCheckScene(RTR_SCENE_KIND_CASTLE, "castle", &castleHash);
    failed |= rtrCheckGeometryRegression("castle", castleHash,
                                         RTR_CHECK_CASTLE_HASH);
    failed |= rtrCheckScene(RTR_SCENE_KIND_CITY100K,
                            "city100k", &city100kHash);
    failed |= rtrCheckSceneDeterminism(RTR_SCENE_KIND_CITY100K,
                                       "city100k", city100kHash);
    failed |= rtrCheckGeometryRegression("city100k", city100kHash,
                                         RTR_CHECK_CITY100K_HASH);
    failed |= rtrCheckScene(RTR_SCENE_KIND_WORLD, "world", &worldHash);
    failed |= rtrCheckSceneDeterminism(RTR_SCENE_KIND_WORLD,
                                       "world", worldHash);
    failed |= rtrCheckGeometryRegression("world", worldHash,
                                         RTR_CHECK_WORLD_HASH);
    return failed ? 1 : 0;
}
