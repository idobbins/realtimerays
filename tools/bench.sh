#!/usr/bin/env bash
# Compile an isolated benchmark variant with extra -D flags, run it headless,
# and print the steady-state GPU timing window without contaminating the
# canonical Ninja outputs.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/.."

# RTR_CC_DEFINES passes extra host defines, e.g. workgroup shape:
#   RTR_CC_DEFINES="-DRTR_TILE_X=16u" tools/bench.sh -DRTR_WG_X=16
# Refresh scene/layout-dependent objects before replacing the shader variants
# below; otherwise a grid change can link a current host to a stale scene.o.
ninja build >/dev/null
bench_dir="${RTR_BENCH_DIR:-build/bench}"
mkdir -p "$bench_dir"

build_pass() {
    glslc -DRTR_RENDER_PASS="$1" "${@:4}" shaders/render.comp \
        -o "$bench_dir/$2.comp.spv"
    xxd -i -n "$3" "$bench_dir/$2.comp.spv" \
        "$bench_dir/$2_comp_spv.h"
}
build_pass 0 render renderCompSpv "$@"
build_pass 1 render_raygen renderRaygenCompSpv "$@"
build_pass 2 render_trace renderTraceCompSpv "$@"
build_pass 3 render_shade renderShadeCompSpv "$@"
build_pass 4 render_shadow renderShadowCompSpv "$@"
build_pass 5 render_resolve renderResolveCompSpv "$@"
cc -std=c99 -Wall -Wextra -pedantic -O2 -I/usr/local/include \
    -I"$bench_dir" -Ibuild ${RTR_CC_DEFINES:-} -c vulkan.c \
    -o "$bench_dir/vulkan.o"
cc -o "$bench_dir/realtimerays" build/main.o "$bench_dir/vulkan.o" build/scene.o \
    build/macos.o -framework AppKit -framework QuartzCore \
    -L/usr/local/lib -Wl,-rpath,/usr/local/lib -lvulkan

icd="${VULKAN_SDK:-}/share/vulkan/icd.d/MoltenVK_icd.json"
if [ -z "${VK_ICD_FILENAMES:-}" ] && [ -f "$icd" ]; then
    export VK_ICD_FILENAMES="$icd"
fi

"$bench_dir/realtimerays" --xpost-raw 1920 1080 220 30 \
    > /dev/null 2> "$bench_dir/timing.txt"
echo "[$*]"
sed -n '2p' "$bench_dir/timing.txt"
