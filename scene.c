#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RTR_SCENE_HDRI_PATH
#define RTR_SCENE_HDRI_PATH "assets/hdri/spaichingen_hill_1024x512_rgba16f.bin"
#endif

#define RTR_SCENE_BRICK_SIZE 4u
#define RTR_SCENE_BRICK_GRID_X 32u
#define RTR_SCENE_BRICK_GRID_Y 12u
#define RTR_SCENE_BRICK_GRID_Z 32u
#define RTR_SCENE_VOXEL_GRID_X (RTR_SCENE_BRICK_GRID_X * RTR_SCENE_BRICK_SIZE)
#define RTR_SCENE_VOXEL_GRID_Y (RTR_SCENE_BRICK_GRID_Y * RTR_SCENE_BRICK_SIZE)
#define RTR_SCENE_VOXEL_GRID_Z (RTR_SCENE_BRICK_GRID_Z * RTR_SCENE_BRICK_SIZE)
#define RTR_SCENE_VOXEL_COUNT \
    (RTR_SCENE_VOXEL_GRID_X * RTR_SCENE_VOXEL_GRID_Y * RTR_SCENE_VOXEL_GRID_Z)
#define RTR_SCENE_BRICK_CAPACITY \
    (RTR_SCENE_BRICK_GRID_X * RTR_SCENE_BRICK_GRID_Y * RTR_SCENE_BRICK_GRID_Z)
#define RTR_SCENE_BRICK_WORDS 2u
#define RTR_SCENE_VOXEL_SIZE 0.055f
#define RTR_SCENE_FLOOR_Y -1.0f
#define RTR_SCENE_CASTLE_OFFSET_X \
    ((int32_t)(RTR_SCENE_VOXEL_GRID_X - 64u) / 2)
#define RTR_SCENE_CASTLE_OFFSET_Z \
    ((int32_t)(RTR_SCENE_VOXEL_GRID_Z - 64u) / 2)
#define RTR_SCENE_ENVMAP_WIDTH 1024u
#define RTR_SCENE_ENVMAP_HEIGHT 512u
#define RTR_SCENE_ENVMAP_WORDS (RTR_SCENE_ENVMAP_WIDTH * RTR_SCENE_ENVMAP_HEIGHT * 2u)

enum {
    RTR_SCENE_BRICK_COUNT_WORD = 8,
    RTR_SCENE_BRICK_GRID_X_WORD = 9,
    RTR_SCENE_BRICK_GRID_Y_WORD = 10,
    RTR_SCENE_BRICK_GRID_Z_WORD = 11,
    RTR_SCENE_QUANT_MIN_WORD = 24,
    RTR_SCENE_QUANT_MAX_WORD = 27,
    RTR_SCENE_VOXEL_MAP_WORD = 32,
    RTR_SCENE_VOXEL_BRICK_WORD =
        RTR_SCENE_VOXEL_MAP_WORD + RTR_SCENE_BRICK_CAPACITY,
    RTR_SCENE_ENVMAP_WORD =
        RTR_SCENE_VOXEL_BRICK_WORD +
        RTR_SCENE_BRICK_CAPACITY * RTR_SCENE_BRICK_WORDS,
};

static uint32_t rtrF32Word(float value)
{
    uint32_t word = 0u;
    memcpy(&word, &value, sizeof(word));
    return word;
}

static uint32_t rtrVoxelIndex(uint32_t x, uint32_t y, uint32_t z)
{
    return (y * RTR_SCENE_VOXEL_GRID_Z + z) * RTR_SCENE_VOXEL_GRID_X + x;
}

static uint32_t rtrBrickIndex(uint32_t bx, uint32_t by, uint32_t bz)
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

static void rtrSetVoxel(uint8_t *voxels, int32_t x, int32_t y, int32_t z)
{
    if (!rtrVoxelInBounds(x, y, z)) return;
    voxels[rtrVoxelIndex((uint32_t)x, (uint32_t)y, (uint32_t)z)] = 1u;
}

static void rtrFillBox(uint8_t *voxels,
                       int32_t x0,
                       int32_t y0,
                       int32_t z0,
                       int32_t x1,
                       int32_t y1,
                       int32_t z1)
{
    for (int32_t y = y0; y <= y1; y++) {
        for (int32_t z = z0; z <= z1; z++) {
            for (int32_t x = x0; x <= x1; x++) {
                rtrSetVoxel(voxels, x, y, z);
            }
        }
    }
}

static void rtrClearBox(uint8_t *voxels,
                        int32_t x0,
                        int32_t y0,
                        int32_t z0,
                        int32_t x1,
                        int32_t y1,
                        int32_t z1)
{
    for (int32_t y = y0; y <= y1; y++) {
        for (int32_t z = z0; z <= z1; z++) {
            for (int32_t x = x0; x <= x1; x++) {
                if (rtrVoxelInBounds(x, y, z))
                    voxels[rtrVoxelIndex((uint32_t)x,
                                         (uint32_t)y,
                                         (uint32_t)z)] = 0u;
            }
        }
    }
}

static void rtrFillCastleBox(uint8_t *voxels,
                             int32_t x0,
                             int32_t y0,
                             int32_t z0,
                             int32_t x1,
                             int32_t y1,
                             int32_t z1)
{
    rtrFillBox(voxels,
               x0 + RTR_SCENE_CASTLE_OFFSET_X,
               y0,
               z0 + RTR_SCENE_CASTLE_OFFSET_Z,
               x1 + RTR_SCENE_CASTLE_OFFSET_X,
               y1,
               z1 + RTR_SCENE_CASTLE_OFFSET_Z);
}

static void rtrClearCastleBox(uint8_t *voxels,
                              int32_t x0,
                              int32_t y0,
                              int32_t z0,
                              int32_t x1,
                              int32_t y1,
                              int32_t z1)
{
    rtrClearBox(voxels,
                x0 + RTR_SCENE_CASTLE_OFFSET_X,
                y0,
                z0 + RTR_SCENE_CASTLE_OFFSET_Z,
                x1 + RTR_SCENE_CASTLE_OFFSET_X,
                y1,
                z1 + RTR_SCENE_CASTLE_OFFSET_Z);
}

static void rtrFillTower(uint8_t *voxels, int32_t cx, int32_t cz, int32_t r, int32_t height)
{
    cx += RTR_SCENE_CASTLE_OFFSET_X;
    cz += RTR_SCENE_CASTLE_OFFSET_Z;
    rtrFillBox(voxels, cx - r, 1, cz - r, cx + r, height, cz + r);
    rtrFillBox(voxels, cx - r - 1, height + 1, cz - r - 1,
               cx + r + 1, height + 3, cz + r + 1);

    for (int32_t i = -r - 1; i <= r + 1; i += 2) {
        rtrFillBox(voxels, cx + i, height + 4, cz - r - 1,
                   cx + i, height + 6, cz - r);
        rtrFillBox(voxels, cx + i, height + 4, cz + r,
                   cx + i, height + 6, cz + r + 1);
        rtrFillBox(voxels, cx - r - 1, height + 4, cz + i,
                   cx - r, height + 6, cz + i);
        rtrFillBox(voxels, cx + r, height + 4, cz + i,
                   cx + r + 1, height + 6, cz + i);
    }
}

static void rtrBuildVoxelScene(uint8_t *voxels)
{
    memset(voxels, 0, RTR_SCENE_VOXEL_COUNT);

    rtrFillBox(voxels,
               0,
               0,
               0,
               (int32_t)RTR_SCENE_VOXEL_GRID_X - 1,
               0,
               (int32_t)RTR_SCENE_VOXEL_GRID_Z - 1);

    rtrFillCastleBox(voxels, 14, 1, 14, 49, 2, 49);
    rtrFillCastleBox(voxels, 15, 3, 15, 48, 9, 17);
    rtrFillCastleBox(voxels, 15, 3, 46, 48, 9, 48);
    rtrFillCastleBox(voxels, 15, 3, 18, 17, 9, 45);
    rtrFillCastleBox(voxels, 46, 3, 18, 48, 9, 45);

    rtrClearCastleBox(voxels, 29, 3, 15, 34, 8, 17);
    rtrClearCastleBox(voxels, 30, 3, 15, 33, 11, 17);

    for (int32_t x = 16; x <= 48; x += 4) {
        rtrFillCastleBox(voxels, x, 10, 15, x + 1, 12, 17);
        rtrFillCastleBox(voxels, x, 10, 46, x + 1, 12, 48);
    }
    for (int32_t z = 20; z <= 44; z += 4) {
        rtrFillCastleBox(voxels, 15, 10, z, 17, 12, z + 1);
        rtrFillCastleBox(voxels, 46, 10, z, 48, 12, z + 1);
    }

    rtrFillTower(voxels, 17, 17, 4, 18);
    rtrFillTower(voxels, 47, 17, 4, 22);
    rtrFillTower(voxels, 17, 47, 4, 20);
    rtrFillTower(voxels, 47, 47, 4, 24);

    rtrFillCastleBox(voxels, 35, 3, 34, 50, 24, 50);
    rtrFillCastleBox(voxels, 37, 25, 36, 48, 28, 48);
    rtrFillCastleBox(voxels, 39, 29, 38, 46, 31, 46);
    rtrClearCastleBox(voxels, 38, 4, 33, 43, 12, 36);
    rtrClearCastleBox(voxels, 45, 4, 38, 50, 13, 43);

    rtrFillCastleBox(voxels, 23, 3, 27, 34, 13, 38);
    rtrFillCastleBox(voxels, 25, 14, 29, 32, 17, 36);
    rtrClearCastleBox(voxels, 27, 4, 26, 30, 10, 29);

    for (int32_t z = 20; z <= 44; z += 8)
        rtrFillCastleBox(voxels, 22, 3, z, 26, 7, z + 3);
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
}

static void rtrStoreSceneBounds(uint32_t *words)
{
    const float extentX = (float)RTR_SCENE_VOXEL_GRID_X * RTR_SCENE_VOXEL_SIZE;
    const float extentY = (float)RTR_SCENE_VOXEL_GRID_Y * RTR_SCENE_VOXEL_SIZE;
    const float extentZ = (float)RTR_SCENE_VOXEL_GRID_Z * RTR_SCENE_VOXEL_SIZE;
    const float sceneMin[3] = {
        -extentX * 0.5f,
        RTR_SCENE_FLOOR_Y,
        -extentZ * 0.5f,
    };
    const float sceneMax[3] = {
        extentX * 0.5f,
        RTR_SCENE_FLOOR_Y + extentY,
        extentZ * 0.5f,
    };

    for (uint32_t axis = 0u; axis < 3u; axis++) {
        words[RTR_SCENE_QUANT_MIN_WORD + axis] = rtrF32Word(sceneMin[axis]);
        words[RTR_SCENE_QUANT_MAX_WORD + axis] = rtrF32Word(sceneMax[axis]);
    }
}

static uint32_t rtrStoreVoxelBricks(uint32_t *words, const uint8_t *voxels)
{
    uint32_t brickCount = 0u;

    memset(words + RTR_SCENE_VOXEL_MAP_WORD,
           0,
           RTR_SCENE_BRICK_CAPACITY * sizeof(uint32_t));
    memset(words + RTR_SCENE_VOXEL_BRICK_WORD,
           0,
           RTR_SCENE_BRICK_CAPACITY * RTR_SCENE_BRICK_WORDS * sizeof(uint32_t));

    for (uint32_t by = 0u; by < RTR_SCENE_BRICK_GRID_Y; by++) {
        for (uint32_t bz = 0u; bz < RTR_SCENE_BRICK_GRID_Z; bz++) {
            for (uint32_t bx = 0u; bx < RTR_SCENE_BRICK_GRID_X; bx++) {
                uint32_t maskLo = 0u;
                uint32_t maskHi = 0u;

                for (uint32_t ly = 0u; ly < RTR_SCENE_BRICK_SIZE; ly++) {
                    for (uint32_t lz = 0u; lz < RTR_SCENE_BRICK_SIZE; lz++) {
                        for (uint32_t lx = 0u; lx < RTR_SCENE_BRICK_SIZE; lx++) {
                            const uint32_t x = bx * RTR_SCENE_BRICK_SIZE + lx;
                            const uint32_t y = by * RTR_SCENE_BRICK_SIZE + ly;
                            const uint32_t z = bz * RTR_SCENE_BRICK_SIZE + lz;
                            const uint32_t bit = lx + lz * 4u + ly * 16u;

                            if (!voxels[rtrVoxelIndex(x, y, z)]) continue;
                            if (bit < 32u)
                                maskLo |= 1u << bit;
                            else
                                maskHi |= 1u << (bit - 32u);
                        }
                    }
                }

                if (!(maskLo || maskHi)) continue;

                const uint32_t brickMap = rtrBrickIndex(bx, by, bz);
                const uint32_t brickWord =
                    RTR_SCENE_VOXEL_BRICK_WORD + brickCount * RTR_SCENE_BRICK_WORDS;
                words[RTR_SCENE_VOXEL_MAP_WORD + brickMap] = brickCount + 1u;
                words[brickWord] = maskLo;
                words[brickWord + 1u] = maskHi;
                brickCount++;
            }
        }
    }

    return brickCount;
}

void rtrScene(uint32_t *words)
{
    uint8_t *voxels = (uint8_t *)malloc(RTR_SCENE_VOXEL_COUNT);
    uint32_t brickCount = 0u;

    words[RTR_SCENE_BRICK_COUNT_WORD] = 0u;
    words[RTR_SCENE_BRICK_GRID_X_WORD] = RTR_SCENE_BRICK_GRID_X;
    words[RTR_SCENE_BRICK_GRID_Y_WORD] = RTR_SCENE_BRICK_GRID_Y;
    words[RTR_SCENE_BRICK_GRID_Z_WORD] = RTR_SCENE_BRICK_GRID_Z;
    rtrStoreSceneBounds(words);

    if (!voxels) {
        fprintf(stderr, "scene: voxel allocation failed\n");
        rtrStoreEnvironment(words);
        return;
    }

    rtrBuildVoxelScene(voxels);
    brickCount = rtrStoreVoxelBricks(words, voxels);
    words[RTR_SCENE_BRICK_COUNT_WORD] = brickCount;
    rtrStoreEnvironment(words);

    free(voxels);
}
