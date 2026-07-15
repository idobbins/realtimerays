#ifndef RTR_SCENE_LAYOUT_H
#define RTR_SCENE_LAYOUT_H

/* Integer-only scene layout shared by C and GLSL. Keep this header free of
 * declarations so shaders can include it through GL_GOOGLE_include_directive. */
#define RTR_LAYOUT_MEMORY_HEADER_WORDS 32u

#define RTR_LAYOUT_BRICK_SIZE 4u
#define RTR_LAYOUT_BRICK_GRID_X 48u
#define RTR_LAYOUT_BRICK_GRID_Y 24u
#define RTR_LAYOUT_BRICK_GRID_Z 48u
#define RTR_LAYOUT_VOXEL_GRID_X \
    (RTR_LAYOUT_BRICK_GRID_X * RTR_LAYOUT_BRICK_SIZE)
#define RTR_LAYOUT_VOXEL_GRID_Y \
    (RTR_LAYOUT_BRICK_GRID_Y * RTR_LAYOUT_BRICK_SIZE)
#define RTR_LAYOUT_VOXEL_GRID_Z \
    (RTR_LAYOUT_BRICK_GRID_Z * RTR_LAYOUT_BRICK_SIZE)
#define RTR_LAYOUT_VOXEL_COUNT \
    (RTR_LAYOUT_VOXEL_GRID_X * RTR_LAYOUT_VOXEL_GRID_Y * \
     RTR_LAYOUT_VOXEL_GRID_Z)

#define RTR_LAYOUT_BRICK_CAPACITY \
    (RTR_LAYOUT_BRICK_GRID_X * RTR_LAYOUT_BRICK_GRID_Y * \
     RTR_LAYOUT_BRICK_GRID_Z)
#define RTR_LAYOUT_BRICK_WORDS 2u
#define RTR_LAYOUT_META_WORDS (RTR_LAYOUT_BRICK_CAPACITY / 2u)

#define RTR_LAYOUT_MATERIAL_BITS 2u
#define RTR_LAYOUT_MATERIAL_WORDS_PER_BRICK 4u
#define RTR_LAYOUT_MATERIAL_WORDS \
    (RTR_LAYOUT_BRICK_CAPACITY * RTR_LAYOUT_MATERIAL_WORDS_PER_BRICK)

#define RTR_LAYOUT_ENVMAP_WIDTH 1024u
#define RTR_LAYOUT_ENVMAP_HEIGHT 512u
#define RTR_LAYOUT_ENVMAP_WORDS \
    (RTR_LAYOUT_ENVMAP_WIDTH * RTR_LAYOUT_ENVMAP_HEIGHT * 2u)
#define RTR_LAYOUT_BLUE_NOISE_WORDS (128u * 128u * 2u)

#define RTR_LAYOUT_META_WORD RTR_LAYOUT_MEMORY_HEADER_WORDS
#define RTR_LAYOUT_VOXEL_BRICK_WORD \
    (RTR_LAYOUT_META_WORD + RTR_LAYOUT_META_WORDS)
#define RTR_LAYOUT_MATERIAL_WORD \
    (RTR_LAYOUT_VOXEL_BRICK_WORD + \
     RTR_LAYOUT_BRICK_CAPACITY * RTR_LAYOUT_BRICK_WORDS)
#define RTR_LAYOUT_ENVMAP_WORD \
    (RTR_LAYOUT_MATERIAL_WORD + RTR_LAYOUT_MATERIAL_WORDS)
#define RTR_LAYOUT_BLUE_NOISE_WORD \
    (RTR_LAYOUT_ENVMAP_WORD + RTR_LAYOUT_ENVMAP_WORDS)
#define RTR_LAYOUT_WF_WORD \
    (RTR_LAYOUT_BLUE_NOISE_WORD + RTR_LAYOUT_BLUE_NOISE_WORDS)
#define RTR_LAYOUT_SCENE_WORDS \
    (RTR_LAYOUT_WF_WORD - RTR_LAYOUT_MEMORY_HEADER_WORDS)

/* Word 31 is the only header slot outside runtime configuration, profiling
 * counters, scene bounds, and the frame counter. */
#define RTR_LAYOUT_SCENE_KIND_WORD 31u
#define RTR_SCENE_KIND_FOREST 0u
#define RTR_SCENE_KIND_CASTLE 1u
#define RTR_SCENE_KIND_CITY100K 2u
#define RTR_SCENE_KIND_WORLD 3u

/* Packed GPU material IDs. CPU staging cells use these values plus one so
 * zero can continue to mean empty. */
#define RTR_MATERIAL_GROUND 0u
#define RTR_MATERIAL_WOOD 1u
#define RTR_MATERIAL_FOLIAGE 2u
#define RTR_MATERIAL_STONE 3u
#define RTR_CELL_EMPTY 0u
#define RTR_CELL_GROUND (RTR_MATERIAL_GROUND + 1u)
#define RTR_CELL_WOOD (RTR_MATERIAL_WOOD + 1u)
#define RTR_CELL_FOLIAGE (RTR_MATERIAL_FOLIAGE + 1u)
#define RTR_CELL_STONE (RTR_MATERIAL_STONE + 1u)

/* Traversal and packing currently rely on these exact relationships. Fail at
 * preprocessing time instead of silently corrupting a changed layout. */
#ifndef RTR_GLSL
#if RTR_LAYOUT_BRICK_SIZE != 4u
#error "voxel mask packing requires 4x4x4 bricks"
#endif
#if RTR_LAYOUT_MATERIAL_BITS != 2u
#error "material packing requires two-bit material IDs"
#endif
#if RTR_LAYOUT_MATERIAL_WORDS_PER_BRICK != \
    ((RTR_LAYOUT_BRICK_SIZE * RTR_LAYOUT_BRICK_SIZE * \
      RTR_LAYOUT_BRICK_SIZE * RTR_LAYOUT_MATERIAL_BITS) / 32u)
#error "material words per brick do not match the packed voxel payload"
#endif
#if (RTR_LAYOUT_BRICK_CAPACITY % 2u) != 0u
#error "two brick metadata records must fit each metadata word"
#endif
#if RTR_LAYOUT_VOXEL_GRID_X > 256u || RTR_LAYOUT_VOXEL_GRID_Y > 256u || \
    RTR_LAYOUT_VOXEL_GRID_Z > 256u
#error "packed hit coordinates are limited to eight bits per axis"
#endif
#if RTR_LAYOUT_SCENE_KIND_WORD != 31u || \
    RTR_LAYOUT_SCENE_KIND_WORD >= RTR_LAYOUT_MEMORY_HEADER_WORDS
#error "scene kind must stay in the collision-free final header word"
#endif
#endif

#endif
