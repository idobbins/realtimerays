#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scene_layout.h"

#ifndef RTR_SCENE_HDRI_PATH
#define RTR_SCENE_HDRI_PATH "assets/hdri/spaichingen_hill_1024x512_rgba16f.bin"
#endif
#ifndef RTR_SCENE_BLUE_NOISE_PATH
#define RTR_SCENE_BLUE_NOISE_PATH "assets/blue_noise/spp1_128x128x8.bin"
#endif

#define RTR_SCENE_BRICK_SIZE RTR_LAYOUT_BRICK_SIZE
#define RTR_SCENE_BRICK_GRID_X RTR_LAYOUT_BRICK_GRID_X
#define RTR_SCENE_BRICK_GRID_Y RTR_LAYOUT_BRICK_GRID_Y
#define RTR_SCENE_BRICK_GRID_Z RTR_LAYOUT_BRICK_GRID_Z
#define RTR_SCENE_VOXEL_GRID_X RTR_LAYOUT_VOXEL_GRID_X
#define RTR_SCENE_VOXEL_GRID_Y RTR_LAYOUT_VOXEL_GRID_Y
#define RTR_SCENE_VOXEL_GRID_Z RTR_LAYOUT_VOXEL_GRID_Z
#define RTR_SCENE_VOXEL_COUNT RTR_LAYOUT_VOXEL_COUNT
#define RTR_SCENE_BRICK_CAPACITY RTR_LAYOUT_BRICK_CAPACITY
#define RTR_SCENE_BRICK_WORDS RTR_LAYOUT_BRICK_WORDS
#define RTR_SCENE_META_WORDS RTR_LAYOUT_META_WORDS
#define RTR_SCENE_MATERIAL_WORDS_PER_BRICK \
    RTR_LAYOUT_MATERIAL_WORDS_PER_BRICK
#define RTR_SCENE_MATERIAL_WORDS RTR_LAYOUT_MATERIAL_WORDS
#define RTR_SCENE_DISTANCE_MAX 15u
#define RTR_SCENE_VOXEL_SIZE 0.055f
#define RTR_SCENE_FLOOR_Y -1.0f
#define RTR_SCENE_CASTLE_OFFSET_X \
    ((int32_t)(RTR_SCENE_VOXEL_GRID_X - 64u) / 2)
#define RTR_SCENE_CASTLE_OFFSET_Z \
    ((int32_t)(RTR_SCENE_VOXEL_GRID_Z - 64u) / 2)
#define RTR_SCENE_CASTLE_FLOOR_SIZE 128u
#define RTR_SCENE_CASTLE_FLOOR_OFFSET_X \
    ((int32_t)(RTR_SCENE_VOXEL_GRID_X - RTR_SCENE_CASTLE_FLOOR_SIZE) / 2)
#define RTR_SCENE_CASTLE_FLOOR_OFFSET_Z \
    ((int32_t)(RTR_SCENE_VOXEL_GRID_Z - RTR_SCENE_CASTLE_FLOOR_SIZE) / 2)
#define RTR_SCENE_FOREST_OFFSET_X \
    ((int32_t)(RTR_SCENE_VOXEL_GRID_X - 128u) / 2)
#define RTR_SCENE_FOREST_OFFSET_Z \
    ((int32_t)(RTR_SCENE_VOXEL_GRID_Z - 128u) / 2)
#define RTR_SCENE_ENVMAP_WIDTH RTR_LAYOUT_ENVMAP_WIDTH
#define RTR_SCENE_ENVMAP_HEIGHT RTR_LAYOUT_ENVMAP_HEIGHT
#define RTR_SCENE_ENVMAP_WORDS RTR_LAYOUT_ENVMAP_WORDS
#define RTR_SCENE_BLUE_NOISE_WORDS RTR_LAYOUT_BLUE_NOISE_WORDS

enum {
    RTR_SCENE_BRICK_COUNT_WORD = 8,
    RTR_SCENE_BRICK_GRID_X_WORD = 9,
    RTR_SCENE_BRICK_GRID_Y_WORD = 10,
    RTR_SCENE_BRICK_GRID_Z_WORD = 11,
    RTR_SCENE_QUANT_MIN_WORD = 24,
    RTR_SCENE_QUANT_MAX_WORD = 27,
    RTR_SCENE_BRICK_META_WORD = RTR_LAYOUT_META_WORD,
    RTR_SCENE_VOXEL_BRICK_WORD =
        RTR_SCENE_BRICK_META_WORD + RTR_SCENE_META_WORDS,
    RTR_SCENE_MATERIAL_WORD =
        RTR_SCENE_VOXEL_BRICK_WORD +
        RTR_SCENE_BRICK_CAPACITY * RTR_SCENE_BRICK_WORDS,
    RTR_SCENE_ENVMAP_WORD =
        RTR_SCENE_MATERIAL_WORD + RTR_SCENE_MATERIAL_WORDS,
    RTR_SCENE_BLUE_NOISE_WORD =
        RTR_SCENE_ENVMAP_WORD + RTR_SCENE_ENVMAP_WORDS,
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

static uint32_t rtrCellPriority(uint8_t cell)
{
    static const uint8_t priorities[] = {0u, 2u, 4u, 1u, 3u};

    return cell < sizeof(priorities) ? priorities[cell] : 0u;
}

static void rtrPutCell(uint8_t *voxels,
                       int32_t x,
                       int32_t y,
                       int32_t z,
                       uint8_t cell)
{
    uint8_t *dst;

    if (!rtrVoxelInBounds(x, y, z)) return;
    dst = &voxels[rtrVoxelIndex((uint32_t)x, (uint32_t)y, (uint32_t)z)];
    if (rtrCellPriority(cell) >= rtrCellPriority(*dst))
        *dst = cell;
}

static void rtrFillBoxMaterial(uint8_t *voxels,
                               int32_t x0,
                               int32_t y0,
                               int32_t z0,
                               int32_t x1,
                               int32_t y1,
                               int32_t z1,
                               uint8_t cell)
{
    for (int32_t y = y0; y <= y1; y++) {
        for (int32_t z = z0; z <= z1; z++) {
            for (int32_t x = x0; x <= x1; x++) {
                rtrPutCell(voxels, x, y, z, cell);
            }
        }
    }
}

static void rtrFillBox(uint8_t *voxels,
                       int32_t x0,
                       int32_t y0,
                       int32_t z0,
                       int32_t x1,
                       int32_t y1,
                       int32_t z1)
{
    rtrFillBoxMaterial(voxels, x0, y0, z0, x1, y1, z1,
                       (uint8_t)RTR_CELL_STONE);
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

static void rtrBuildCastleScene(uint8_t *voxels)
{
    memset(voxels, 0, RTR_SCENE_VOXEL_COUNT);

    rtrFillBoxMaterial(voxels,
                       RTR_SCENE_CASTLE_FLOOR_OFFSET_X,
                       0,
                       RTR_SCENE_CASTLE_FLOOR_OFFSET_Z,
                       RTR_SCENE_CASTLE_FLOOR_OFFSET_X +
                           (int32_t)RTR_SCENE_CASTLE_FLOOR_SIZE - 1,
                       0,
                       RTR_SCENE_CASTLE_FLOOR_OFFSET_Z +
                           (int32_t)RTR_SCENE_CASTLE_FLOOR_SIZE - 1,
                       (uint8_t)RTR_CELL_GROUND);

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

typedef struct {
    int32_t x;
    int32_t z;
    int32_t trunkHeight;
    int32_t baseRadius;
    int32_t crownY;
    int32_t crownRx;
    int32_t crownRy;
    int32_t crownRz;
    int32_t lobeCount;
    int32_t bendX;
    int32_t bendZ;
    uint32_t seed;
} RTRTreeSpec;

static const RTRTreeSpec rtrForestTrees[] = {
    {34, 43, 69, 3, 58, 10, 12, 9, 8, 4, 2, 0x1a2b3c41u},
    {50, 29, 55, 2, 47, 8, 9, 7, 6, 3, 1, 0x4d5e6f74u},
    {25, 64, 54, 2, 46, 8, 9, 7, 6, 2, 2, 0x92a3b4c9u},
    {45, 51, 40, 1, 34, 6, 7, 6, 5, 2, 1, 0xa3b4c5dau},
    {95, 44, 66, 3, 56, 10, 11, 10, 8, -3, 4, 0x2b3c4d52u},
    {76, 30, 59, 2, 50, 7, 9, 8, 6, -2, 3, 0x5e6f7085u},
    {104, 62, 52, 2, 44, 8, 8, 7, 6, -3, -1, 0x6f708196u},
    {84, 50, 43, 2, 36, 6, 7, 6, 5, -2, 1, 0xb4c5d6ebu},
    {67, 98, 71, 3, 60, 11, 11, 9, 8, 2, -4, 0x3c4d5e63u},
    {91, 91, 56, 2, 47, 8, 9, 8, 6, 2, -3, 0x708192a7u},
    {42, 92, 51, 2, 43, 7, 8, 8, 6, 3, -2, 0x8192a3b8u},
    {66, 86, 38, 1, 32, 6, 7, 6, 5, 1, -2, 0xc5d6e7fcu},
};

static const int8_t rtrForestDirections[16][2] = {
    {8, 0}, {7, 3}, {6, 6}, {3, 7},
    {0, 8}, {-3, 7}, {-6, 6}, {-7, 3},
    {-8, 0}, {-7, -3}, {-6, -6}, {-3, -7},
    {0, -8}, {3, -7}, {6, -6}, {7, -3},
};

static uint32_t rtrSceneHash(uint32_t value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

static uint32_t rtrNextRandom(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static int32_t rtrRoundDivSigned(int32_t numerator, int32_t denominator)
{
    if (denominator <= 0) return 0;
    if (numerator >= 0)
        return (numerator + denominator / 2) / denominator;
    return -((-numerator + denominator / 2) / denominator);
}

static void rtrStampDiskXZ(uint8_t *voxels,
                           int32_t cx,
                           int32_t y,
                           int32_t cz,
                           int32_t radius,
                           uint8_t cell)
{
    const int32_t radius2 = radius * radius;

    for (int32_t z = cz - radius; z <= cz + radius; z++) {
        for (int32_t x = cx - radius; x <= cx + radius; x++) {
            const int32_t dx = x - cx;
            const int32_t dz = z - cz;
            if (dx * dx + dz * dz <= radius2)
                rtrPutCell(voxels, x, y, z, cell);
        }
    }
}

static void rtrStampEllipseXZ(uint8_t *voxels,
                              int32_t cx,
                              int32_t y,
                              int32_t cz,
                              int32_t rx,
                              int32_t rz,
                              uint8_t cell)
{
    const int64_t rx2 = (int64_t)rx * rx;
    const int64_t rz2 = (int64_t)rz * rz;
    const int64_t limit = rx2 * rz2;

    for (int32_t z = cz - rz; z <= cz + rz; z++) {
        for (int32_t x = cx - rx; x <= cx + rx; x++) {
            const int64_t dx = (int64_t)x - cx;
            const int64_t dz = (int64_t)z - cz;
            if (dx * dx * rz2 + dz * dz * rx2 <= limit)
                rtrPutCell(voxels, x, y, z, cell);
        }
    }
}

static void rtrStampEllipsoid(uint8_t *voxels,
                              int32_t cx,
                              int32_t cy,
                              int32_t cz,
                              int32_t rx,
                              int32_t ry,
                              int32_t rz,
                              uint8_t cell,
                              uint32_t seed)
{
    const int64_t rx2 = (int64_t)rx * rx;
    const int64_t ry2 = (int64_t)ry * ry;
    const int64_t rz2 = (int64_t)rz * rz;
    const int64_t limit = rx2 * ry2 * rz2;

    if (rx < 0 || ry < 0 || rz < 0) return;
    for (int32_t y = cy - ry; y <= cy + ry; y++) {
        for (int32_t z = cz - rz; z <= cz + rz; z++) {
            for (int32_t x = cx - rx; x <= cx + rx; x++) {
                const int64_t dx = (int64_t)x - cx;
                const int64_t dy = (int64_t)y - cy;
                const int64_t dz = (int64_t)z - cz;
                const int64_t distance =
                    dx * dx * ry2 * rz2 +
                    dy * dy * rx2 * rz2 +
                    dz * dz * rx2 * ry2;

                if (distance > limit) continue;
                if (cell == RTR_CELL_FOLIAGE) {
                    const uint32_t coarse =
                        (uint32_t)(x >> 1) * 73856093u ^
                        (uint32_t)(y >> 1) * 19349663u ^
                        (uint32_t)(z >> 1) * 83492791u ^ seed;
                    if ((rtrSceneHash(coarse) & 15u) < 3u) continue;
                }
                rtrPutCell(voxels, x, y, z, cell);
            }
        }
    }
}

static void rtrStampLine(uint8_t *voxels,
                         int32_t x0,
                         int32_t y0,
                         int32_t z0,
                         int32_t x1,
                         int32_t y1,
                         int32_t z1,
                         int32_t radius,
                         uint8_t cell)
{
    const int32_t dx = x1 - x0;
    const int32_t dy = y1 - y0;
    const int32_t dz = z1 - z0;
    int32_t steps = abs(dx);

    if (abs(dy) > steps) steps = abs(dy);
    if (abs(dz) > steps) steps = abs(dz);
    if (steps == 0) {
        rtrStampEllipsoid(voxels, x0, y0, z0, radius, radius, radius,
                          cell, 0u);
        return;
    }

    for (int32_t i = 0; i <= steps; i++) {
        const int32_t x = x0 + rtrRoundDivSigned(dx * i, steps);
        const int32_t y = y0 + rtrRoundDivSigned(dy * i, steps);
        const int32_t z = z0 + rtrRoundDivSigned(dz * i, steps);
        rtrStampEllipsoid(voxels, x, y, z, radius, radius, radius,
                          cell, 0u);
    }
}

static void rtrTreePosition(const RTRTreeSpec *tree,
                            int32_t y,
                            int32_t *x,
                            int32_t *z)
{
    const int32_t denominator = tree->trunkHeight * tree->trunkHeight;
    const int32_t y2 = y * y;

    *x = tree->x + RTR_SCENE_FOREST_OFFSET_X +
        rtrRoundDivSigned(tree->bendX * y2, denominator);
    *z = tree->z + RTR_SCENE_FOREST_OFFSET_Z +
        rtrRoundDivSigned(tree->bendZ * y2, denominator);
}

static void rtrStampTree(uint8_t *voxels, const RTRTreeSpec *tree)
{
    int32_t lobeX[8];
    int32_t lobeY[8];
    int32_t lobeZ[8];
    int32_t lobeCount = tree->lobeCount;
    uint32_t random = tree->seed;

    if (lobeCount > 8) lobeCount = 8;
    rtrTreePosition(tree, tree->crownY, &lobeX[0], &lobeZ[0]);
    lobeY[0] = tree->crownY + 2;
    rtrStampEllipsoid(voxels,
                      lobeX[0], lobeY[0], lobeZ[0],
                      tree->crownRx * 7 / 10,
                      tree->crownRy * 7 / 10,
                      tree->crownRz * 7 / 10,
                      (uint8_t)RTR_CELL_FOLIAGE,
                      tree->seed);

    for (int32_t i = 1; i < lobeCount; i++) {
        const uint32_t value = rtrNextRandom(&random);
        const int32_t direction =
            ((int32_t)(tree->seed & 15u) +
             (i - 1) * 16 / (lobeCount - 1)) & 15;
        const int32_t radial = 48 + (int32_t)(value & 15u);
        const int32_t rx = tree->crownRx * (50 + (int32_t)((value >> 4u) & 15u)) / 100;
        const int32_t ry = tree->crownRy * (50 + (int32_t)((value >> 8u) & 15u)) / 100;
        const int32_t rz = tree->crownRz * (50 + (int32_t)((value >> 12u) & 15u)) / 100;

        lobeX[i] = lobeX[0] +
            rtrForestDirections[direction][0] * tree->crownRx * radial / 800;
        lobeZ[i] = lobeZ[0] +
            rtrForestDirections[direction][1] * tree->crownRz * radial / 800;
        lobeY[i] = tree->crownY - tree->crownRy / 3 +
            (int32_t)((value >> 16u) % (uint32_t)(tree->crownRy + 1));
        rtrStampEllipsoid(voxels,
                          lobeX[i], lobeY[i], lobeZ[i],
                          rx > 2 ? rx : 2,
                          ry > 2 ? ry : 2,
                          rz > 2 ? rz : 2,
                          (uint8_t)RTR_CELL_FOLIAGE,
                          tree->seed);
    }

    for (int32_t i = 1; i < lobeCount; i++) {
        int32_t branchX;
        int32_t branchZ;
        const int32_t branchY = tree->crownY - tree->crownRy / 3 + i % 4;
        const int32_t branchRadius = tree->baseRadius >= 3 && i <= 3 ? 1 : 0;

        rtrTreePosition(tree, branchY, &branchX, &branchZ);
        if (branchRadius > 0) {
            const int32_t splitX = branchX +
                rtrRoundDivSigned((lobeX[i] - branchX) * 3, 5);
            const int32_t splitY = branchY +
                rtrRoundDivSigned((lobeY[i] - branchY) * 3, 5);
            const int32_t splitZ = branchZ +
                rtrRoundDivSigned((lobeZ[i] - branchZ) * 3, 5);

            rtrStampLine(voxels,
                         branchX, branchY, branchZ,
                         splitX, splitY, splitZ,
                         branchRadius,
                         (uint8_t)RTR_CELL_WOOD);
            rtrStampLine(voxels,
                         splitX, splitY, splitZ,
                         lobeX[i], lobeY[i], lobeZ[i],
                         0,
                         (uint8_t)RTR_CELL_WOOD);
        } else {
            rtrStampLine(voxels,
                         branchX, branchY, branchZ,
                         lobeX[i], lobeY[i], lobeZ[i],
                         0,
                         (uint8_t)RTR_CELL_WOOD);
        }
    }

    for (int32_t y = 1; y <= tree->trunkHeight; y++) {
        int32_t x;
        int32_t z;
        int32_t radius = 1;

        rtrTreePosition(tree, y, &x, &z);
        if (y <= 4)
            radius = tree->baseRadius + 1;
        else if (y * 5 <= tree->trunkHeight)
            radius = tree->baseRadius;
        else if (y * 3 <= tree->trunkHeight * 2)
            radius = tree->baseRadius > 1 ? tree->baseRadius - 1 : 1;
        rtrStampDiskXZ(voxels, x, y, z, radius,
                       (uint8_t)RTR_CELL_WOOD);
    }

    const int32_t rootCount = tree->baseRadius >= 3 ? 5 : 4;
    for (int32_t root = 0; root < rootCount; root++) {
        const int32_t direction =
            ((int32_t)(tree->seed & 15u) + root * 16 / rootCount) & 15;
        const int32_t length = 5 + tree->baseRadius + root;
        const int32_t rootX = tree->x + RTR_SCENE_FOREST_OFFSET_X;
        const int32_t rootZ = tree->z + RTR_SCENE_FOREST_OFFSET_Z;
        const int32_t x1 = rootX +
            rtrForestDirections[direction][0] * length / 8;
        const int32_t z1 = rootZ +
            rtrForestDirections[direction][1] * length / 8;

        rtrStampLine(voxels, rootX, 3, rootZ, x1, 1, z1, 1,
                     (uint8_t)RTR_CELL_WOOD);
    }
}

static void rtrCullIsolatedFoliage(uint8_t *voxels)
{
    uint8_t *remove = (uint8_t *)calloc(RTR_SCENE_VOXEL_COUNT, 1u);
    static const int8_t offsets[6][3] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
        {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
    };

    if (!remove) return;
    for (uint32_t pass = 0u; pass < 8u; pass++) {
        uint32_t removeCount = 0u;

        memset(remove, 0, RTR_SCENE_VOXEL_COUNT);
        for (int32_t y = 1; y + 1 < (int32_t)RTR_SCENE_VOXEL_GRID_Y; y++) {
            for (int32_t z = 1; z + 1 < (int32_t)RTR_SCENE_VOXEL_GRID_Z; z++) {
                for (int32_t x = 1; x + 1 < (int32_t)RTR_SCENE_VOXEL_GRID_X; x++) {
                    const uint32_t index = rtrVoxelIndex((uint32_t)x,
                                                         (uint32_t)y,
                                                         (uint32_t)z);
                    uint32_t neighbors = 0u;

                    if (voxels[index] != RTR_CELL_FOLIAGE) continue;
                    for (uint32_t i = 0u; i < 6u; i++) {
                        const uint8_t cell = voxels[rtrVoxelIndex(
                            (uint32_t)(x + offsets[i][0]),
                            (uint32_t)(y + offsets[i][1]),
                            (uint32_t)(z + offsets[i][2]))];
                        neighbors += cell == RTR_CELL_FOLIAGE ||
                                     cell == RTR_CELL_WOOD;
                    }
                    if (neighbors < 2u) {
                        remove[index] = 1u;
                        removeCount++;
                    }
                }
            }
        }
        for (uint32_t i = 0u; i < RTR_SCENE_VOXEL_COUNT; i++) {
            if (remove[i]) voxels[i] = RTR_CELL_EMPTY;
        }
        if (removeCount == 0u) break;
    }
    free(remove);
}

static void rtrBuildForestScene(uint8_t *voxels)
{
    static const int16_t hummocks[][4] = {
        {32, 35, 7, 5}, {57, 24, 8, 6}, {86, 30, 7, 5},
        {108, 50, 6, 7}, {105, 82, 7, 5}, {85, 103, 8, 5},
        {53, 105, 7, 6}, {26, 83, 6, 5}, {22, 53, 6, 7},
    };
    const int32_t ox = RTR_SCENE_FOREST_OFFSET_X;
    const int32_t oz = RTR_SCENE_FOREST_OFFSET_Z;

    memset(voxels, 0, RTR_SCENE_VOXEL_COUNT);
    rtrFillBoxMaterial(voxels,
                       0, 0, 0,
                       (int32_t)RTR_SCENE_VOXEL_GRID_X - 1,
                       0,
                       (int32_t)RTR_SCENE_VOXEL_GRID_Z - 1,
                       (uint8_t)RTR_CELL_GROUND);

    for (uint32_t i = 0u; i < sizeof(hummocks) / sizeof(hummocks[0]); i++) {
        rtrStampEllipseXZ(voxels,
                          hummocks[i][0] + ox, 1, hummocks[i][1] + oz,
                          hummocks[i][2] * 2 / 3,
                          hummocks[i][3] * 2 / 3,
                          (uint8_t)RTR_CELL_GROUND);
    }
    for (uint32_t i = 0u;
         i < sizeof(rtrForestTrees) / sizeof(rtrForestTrees[0]);
         i++) {
        rtrStampTree(voxels, &rtrForestTrees[i]);
    }

    rtrStampLine(voxels, 50 + ox, 3, 79 + oz,
                 70 + ox, 4, 84 + oz, 2,
                 (uint8_t)RTR_CELL_WOOD);
    for (int32_t y = 1; y <= 6; y++) {
        rtrStampDiskXZ(voxels, 83 + ox, y, 77 + oz, y < 4 ? 3 : 2,
                       (uint8_t)RTR_CELL_WOOD);
    }
    rtrStampEllipsoid(voxels, 30 + ox, 2, 82 + oz, 4, 2, 3,
                      (uint8_t)RTR_CELL_STONE, 0u);
    rtrStampEllipsoid(voxels, 80 + ox, 1, 35 + oz, 3, 1, 2,
                      (uint8_t)RTR_CELL_STONE, 0u);
    rtrStampEllipsoid(voxels, 99 + ox, 2, 77 + oz, 4, 2, 3,
                      (uint8_t)RTR_CELL_STONE, 0u);
    rtrCullIsolatedFoliage(voxels);
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

static void rtrStoreBlueNoise(uint32_t *words)
{
    FILE *file = fopen(RTR_SCENE_BLUE_NOISE_PATH, "rb");
    size_t readCount = 0u;

    if (file) {
        readCount = fread(words + RTR_SCENE_BLUE_NOISE_WORD,
                          sizeof(uint32_t),
                          RTR_SCENE_BLUE_NOISE_WORDS,
                          file);
        fclose(file);
    }
    if (readCount == RTR_SCENE_BLUE_NOISE_WORDS) return;

    fprintf(stderr, "scene: failed to read %s, using hashed samples\n",
            RTR_SCENE_BLUE_NOISE_PATH);
    for (uint32_t i = 0u; i < RTR_SCENE_BLUE_NOISE_WORDS; i++) {
        uint32_t x = i + 0x9e3779b9u;

        x ^= x >> 16u;
        x *= 0x7feb352du;
        x ^= x >> 15u;
        x *= 0x846ca68bu;
        words[RTR_SCENE_BLUE_NOISE_WORD + i] = x ^ (x >> 16u);
    }
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

/* Two-pass 26-neighbor chamfer computes the exact Chebyshev distance from
 * every empty brick to the nearest occupied one; the shader uses it to
 * jump rays across guaranteed-empty boxes instead of stepping brick by
 * brick. The distance shares a 16-bit meta record with the brick's
 * occupied-voxel AABB so traversal reads both in one load. */
static void rtrStoreBrickDistance(uint32_t *words, const uint8_t *dist0)
{
    uint8_t *dist = (uint8_t *)malloc(RTR_SCENE_BRICK_CAPACITY);

    if (!dist) {
        fprintf(stderr, "scene: distance allocation failed\n");
        return;
    }

    memcpy(dist, dist0, RTR_SCENE_BRICK_CAPACITY);

    for (uint32_t pass = 0u; pass < 2u; pass++) {
        const int32_t dir = pass == 0u ? 1 : -1;

        for (int32_t sy = 0; sy < (int32_t)RTR_SCENE_BRICK_GRID_Y; sy++) {
            for (int32_t sz = 0; sz < (int32_t)RTR_SCENE_BRICK_GRID_Z; sz++) {
                for (int32_t sx = 0; sx < (int32_t)RTR_SCENE_BRICK_GRID_X; sx++) {
                    const int32_t by = pass == 0u ?
                        sy : (int32_t)RTR_SCENE_BRICK_GRID_Y - 1 - sy;
                    const int32_t bz = pass == 0u ?
                        sz : (int32_t)RTR_SCENE_BRICK_GRID_Z - 1 - sz;
                    const int32_t bx = pass == 0u ?
                        sx : (int32_t)RTR_SCENE_BRICK_GRID_X - 1 - sx;
                    uint8_t best = dist[rtrBrickIndex((uint32_t)bx,
                                                      (uint32_t)by,
                                                      (uint32_t)bz)];

                    for (int32_t ny = -1; ny <= 1; ny++) {
                        for (int32_t nz = -1; nz <= 1; nz++) {
                            for (int32_t nx = -1; nx <= 1; nx++) {
                                const int32_t scan =
                                    (ny * (int32_t)RTR_SCENE_BRICK_GRID_Z + nz) *
                                    (int32_t)RTR_SCENE_BRICK_GRID_X + nx;
                                const int32_t qx = bx + nx * dir;
                                const int32_t qy = by + ny * dir;
                                const int32_t qz = bz + nz * dir;

                                if (scan >= 0) continue;
                                if (qx < 0 || qy < 0 || qz < 0 ||
                                    qx >= (int32_t)RTR_SCENE_BRICK_GRID_X ||
                                    qy >= (int32_t)RTR_SCENE_BRICK_GRID_Y ||
                                    qz >= (int32_t)RTR_SCENE_BRICK_GRID_Z) {
                                    continue;
                                }

                                const uint8_t candidate =
                                    dist[rtrBrickIndex((uint32_t)qx,
                                                       (uint32_t)qy,
                                                       (uint32_t)qz)];
                                if (candidate < 255u &&
                                    (uint8_t)(candidate + 1u) < best)
                                    best = (uint8_t)(candidate + 1u);
                            }
                        }
                    }

                    dist[rtrBrickIndex((uint32_t)bx,
                                       (uint32_t)by,
                                       (uint32_t)bz)] = best;
                }
            }
        }
    }

    for (uint32_t i = 0u; i < RTR_SCENE_BRICK_CAPACITY; i++) {
        const uint32_t clamped = dist[i] > RTR_SCENE_DISTANCE_MAX ?
            RTR_SCENE_DISTANCE_MAX : dist[i];
        words[RTR_SCENE_BRICK_META_WORD + i / 2u] |=
            clamped << ((i % 2u) * 16u);
    }

    free(dist);
}

/* Masks are stored dense (indexed by brick coordinate) so traversal needs
 * no map indirection. */
static uint32_t rtrStoreVoxelBricks(uint32_t *words, const uint8_t *voxels)
{
    uint8_t *dist0 = (uint8_t *)malloc(RTR_SCENE_BRICK_CAPACITY);
    uint32_t brickCount = 0u;

    if (!dist0) {
        fprintf(stderr, "scene: occupancy allocation failed\n");
        return 0u;
    }

    memset(dist0, 255, RTR_SCENE_BRICK_CAPACITY);
    memset(words + RTR_SCENE_VOXEL_BRICK_WORD,
           0,
           RTR_SCENE_BRICK_CAPACITY * RTR_SCENE_BRICK_WORDS * sizeof(uint32_t));
    memset(words + RTR_SCENE_MATERIAL_WORD,
           0,
           RTR_SCENE_MATERIAL_WORDS * sizeof(uint32_t));
    memset(words + RTR_SCENE_BRICK_META_WORD,
           0,
           RTR_SCENE_META_WORDS * sizeof(uint32_t));

    for (uint32_t by = 0u; by < RTR_SCENE_BRICK_GRID_Y; by++) {
        for (uint32_t bz = 0u; bz < RTR_SCENE_BRICK_GRID_Z; bz++) {
            for (uint32_t bx = 0u; bx < RTR_SCENE_BRICK_GRID_X; bx++) {
                const uint32_t brickIndex = rtrBrickIndex(bx, by, bz);
                uint32_t maskLo = 0u;
                uint32_t maskHi = 0u;
                uint32_t boundsMin[3] = {3u, 3u, 3u};
                uint32_t boundsMax[3] = {0u, 0u, 0u};

                for (uint32_t ly = 0u; ly < RTR_SCENE_BRICK_SIZE; ly++) {
                    for (uint32_t lz = 0u; lz < RTR_SCENE_BRICK_SIZE; lz++) {
                        for (uint32_t lx = 0u; lx < RTR_SCENE_BRICK_SIZE; lx++) {
                            const uint32_t x = bx * RTR_SCENE_BRICK_SIZE + lx;
                            const uint32_t y = by * RTR_SCENE_BRICK_SIZE + ly;
                            const uint32_t z = bz * RTR_SCENE_BRICK_SIZE + lz;
                            const uint32_t bit = lx + lz * 4u + ly * 16u;
                            const uint8_t cell =
                                voxels[rtrVoxelIndex(x, y, z)];

                            if (cell == RTR_CELL_EMPTY) continue;
                            if (bit < 32u)
                                maskLo |= 1u << bit;
                            else
                                maskHi |= 1u << (bit - 32u);
                            words[RTR_SCENE_MATERIAL_WORD +
                                  brickIndex *
                                      RTR_SCENE_MATERIAL_WORDS_PER_BRICK +
                                  bit / 16u] |=
                                (uint32_t)(cell - 1u) << ((bit % 16u) * 2u);
                            if (lx < boundsMin[0]) boundsMin[0] = lx;
                            if (ly < boundsMin[1]) boundsMin[1] = ly;
                            if (lz < boundsMin[2]) boundsMin[2] = lz;
                            if (lx > boundsMax[0]) boundsMax[0] = lx;
                            if (ly > boundsMax[1]) boundsMax[1] = ly;
                            if (lz > boundsMax[2]) boundsMax[2] = lz;
                        }
                    }
                }

                if (!(maskLo || maskHi)) continue;

                const uint32_t brickWord = RTR_SCENE_VOXEL_BRICK_WORD +
                    brickIndex * RTR_SCENE_BRICK_WORDS;
                const uint32_t bounds =
                    boundsMin[0] | (boundsMin[1] << 2u) | (boundsMin[2] << 4u) |
                    (boundsMax[0] << 6u) | (boundsMax[1] << 8u) |
                    (boundsMax[2] << 10u);
                dist0[brickIndex] = 0u;
                words[brickWord] = maskLo;
                words[brickWord + 1u] = maskHi;
                words[RTR_SCENE_BRICK_META_WORD + brickIndex / 2u] |=
                    (bounds << 4u) << ((brickIndex % 2u) * 16u);
                brickCount++;
            }
        }
    }

    rtrStoreBrickDistance(words, dist0);
    free(dist0);

    return brickCount;
}

void rtrSceneBuild(uint32_t *words, uint32_t sceneKind)
{
    uint8_t *voxels = (uint8_t *)malloc(RTR_SCENE_VOXEL_COUNT);
    uint32_t brickCount = 0u;

    if (sceneKind != RTR_SCENE_KIND_CASTLE)
        sceneKind = RTR_SCENE_KIND_FOREST;
    words[RTR_SCENE_BRICK_COUNT_WORD] = 0u;
    words[RTR_SCENE_BRICK_GRID_X_WORD] = RTR_SCENE_BRICK_GRID_X;
    words[RTR_SCENE_BRICK_GRID_Y_WORD] = RTR_SCENE_BRICK_GRID_Y;
    words[RTR_SCENE_BRICK_GRID_Z_WORD] = RTR_SCENE_BRICK_GRID_Z;
    words[RTR_LAYOUT_SCENE_KIND_WORD] = sceneKind;
    rtrStoreSceneBounds(words);
    rtrStoreBlueNoise(words);

    if (!voxels) {
        fprintf(stderr, "scene: voxel allocation failed\n");
        rtrStoreEnvironment(words);
        return;
    }

    if (sceneKind == RTR_SCENE_KIND_CASTLE)
        rtrBuildCastleScene(voxels);
    else
        rtrBuildForestScene(voxels);
    brickCount = rtrStoreVoxelBricks(words, voxels);
    words[RTR_SCENE_BRICK_COUNT_WORD] = brickCount;
    rtrStoreEnvironment(words);

    free(voxels);
}

void rtrScene(uint32_t *words)
{
    const char *scene = getenv("RTR_SCENE");
    const uint32_t sceneKind = scene && strcmp(scene, "castle") == 0 ?
        RTR_SCENE_KIND_CASTLE : RTR_SCENE_KIND_FOREST;

    rtrSceneBuild(words, sceneKind);
}
