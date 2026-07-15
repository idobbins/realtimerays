#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 6 ]; then
    echo "usage: $0 monolith raygen trace shade shadow resolve" >&2
    exit 2
fi
if ! command -v spirv-val >/dev/null 2>&1; then
    echo "shader-check: spirv-val is required" >&2
    exit 2
fi
if ! command -v spirv-dis >/dev/null 2>&1; then
    echo "shader-check: spirv-dis is required" >&2
    exit 2
fi

for shader in "$@"; do
    spirv-val "$shader"
done

has_material_lookup() {
    spirv-dis "$1" | awk '
        /rtrVoxelMaterial/ { found = 1 }
        END { exit found ? 0 : 1 }
    '
}

if ! has_material_lookup "$1"; then
    echo "shader-check: monolith lost post-hit material resolution" >&2
    exit 1
fi
if ! has_material_lookup "$4"; then
    echo "shader-check: shade pass lost post-hit material resolution" >&2
    exit 1
fi
for shader in "$2" "$3" "$5" "$6"; do
    if has_material_lookup "$shader"; then
        echo "shader-check: material resolution leaked into $shader" >&2
        exit 1
    fi
done

echo "shader-check: SPIR-V valid; materials resolve only after confirmed hits"
