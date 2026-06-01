#!/usr/bin/env sh
set -eu

bin="${1:?usage: xpost-macos.sh BIN}"
seconds="${XPOST_SECONDS:-30}"
fps="${XPOST_FPS:-30}"
width="${XPOST_WIDTH:-1280}"
height="${XPOST_HEIGHT:-720}"
frames=$((seconds * fps))
out_dir="${XPOST_DIR:-build/xpost}"
mkdir -p "$out_dir"

stem="$out_dir/realtimerays-$(date +%Y%m%d-%H%M%S)"
mp4_out="${XPOST_OUT:-$stem.mp4}"
icd="${VULKAN_SDK:-}/share/vulkan/icd.d/MoltenVK_icd.json"

printf 'Rendering %ux%u %us @ %u fps to %s\n' "$width" "$height" "$seconds" "$fps" "$mp4_out"
if [ -z "${VK_ICD_FILENAMES:-}" ] && [ -f "$icd" ]; then
    VK_ICD_FILENAMES="$icd" "$bin" --xpost-raw "$width" "$height" "$frames" "$fps"
else
    "$bin" --xpost-raw "$width" "$height" "$frames" "$fps"
fi | ffmpeg -hide_banner -loglevel error -y \
    -f rawvideo -pix_fmt rgba -s "${width}x${height}" -r "$fps" -i - \
    -c:v libx264 -profile:v high -level 4.0 -preset slow -crf "${XPOST_CRF:-20}" \
    -pix_fmt yuv420p \
    -movflags +faststart \
    -an \
    "$mp4_out"

printf 'Saved %s\n' "$mp4_out"
