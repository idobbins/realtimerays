#!/usr/bin/env bash
# Compile the render shader with extra -D flags, rebuild the binary
# directly (bypassing ninja so the shader header can't go stale), run the
# headless benchmark, and print the steady-state GPU timing window.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/.."

# RTR_CC_DEFINES passes extra host defines, e.g. workgroup shape:
#   RTR_CC_DEFINES="-DRTR_TILE_X=16u" tools/bench.sh -DRTR_WG_X=16
glslc -DRTR_RENDER_PASS=0 "$@" shaders/render.comp -o build/render.comp.spv
xxd -i -n renderCompSpv build/render.comp.spv > build/render_comp_spv.h
cc -std=c99 -Wall -Wextra -pedantic -O2 -I/usr/local/include -Ibuild \
    ${RTR_CC_DEFINES:-} -c vulkan.c -o build/vulkan.o
cc -o build/realtimerays build/main.o build/vulkan.o build/scene.o \
    build/macos.o -framework AppKit -framework QuartzCore \
    -L/usr/local/lib -Wl,-rpath,/usr/local/lib -lvulkan

icd="${VULKAN_SDK:-}/share/vulkan/icd.d/MoltenVK_icd.json"
if [ -z "${VK_ICD_FILENAMES:-}" ] && [ -f "$icd" ]; then
    export VK_ICD_FILENAMES="$icd"
fi

./build/realtimerays --xpost-raw 1920 1080 220 30 > /dev/null 2> /tmp/rtr_bench.txt
echo "[$*]"
sed -n '2p' /tmp/rtr_bench.txt
