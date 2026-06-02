#include <stdint.h>
#include <string.h>

enum {
    RTR_SCENE_COUNT_WORD = 8,
    RTR_SCENE_WORD = 16,
    RTR_SCENE_SPHERE_COUNT = 100,
    RTR_SCENE_SPHERE_WORDS = 8,
};

void rtrScene(uint32_t *words)
{
    words[RTR_SCENE_COUNT_WORD] = RTR_SCENE_SPHERE_COUNT;

    for (uint32_t z = 0u; z < 10u; z++) {
        for (uint32_t x = 0u; x < 10u; x++) {
            const uint32_t i = z * 10u + x;
            const float fx = ((float)x - 4.5f) * 0.42f;
            const float fz = ((float)z - 4.5f) * 0.42f;
            const float r = 0.10f + (float)((x * 3u + z * 5u) % 5u) * 0.025f;
            const float u = (float)x / 9.0f;
            const float v = (float)z / 9.0f;
            const float w = (float)((x + z) % 10u) / 9.0f;
            const float sphere[RTR_SCENE_SPHERE_WORDS] = {
                fx, -1.0f + r, fz, r,
                0.24f + 0.56f * u, 0.25f + 0.38f * w, 0.34f + 0.42f * v, 0.0f,
            };

            memcpy(words + RTR_SCENE_WORD + i * RTR_SCENE_SPHERE_WORDS,
                   sphere, sizeof(sphere));
        }
    }
}
