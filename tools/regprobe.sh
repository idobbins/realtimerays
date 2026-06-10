#!/usr/bin/env bash
# Print Metal's register-pressure verdict (maxTotalThreadsPerThreadgroup)
# for the render shader compiled with the given glslc -D flags.
# Requires spirv-cross and Xcode. Higher is better; 1024 is unconstrained.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/.."

bin="$(mktemp)"
cc -framework Metal -framework Foundation -x objective-c \
    -o "$bin" tools/pipeinfo.m
glslc -DRTR_RENDER_PASS=0 "$@" shaders/render.comp -o /tmp/regprobe.spv
spirv-cross --msl --msl-version 20300 /tmp/regprobe.spv \
    --output /tmp/regprobe.metal
"$bin" /tmp/regprobe.metal
rm -f "$bin" /tmp/regprobe.spv /tmp/regprobe.metal
