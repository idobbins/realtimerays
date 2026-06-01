#!/usr/bin/env bash
set -euo pipefail

old_pwd="$PWD"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"
cd "$repo_dir"

if ! command -v ffmpeg >/dev/null 2>&1; then
    printf 'ffmpeg is required for xpost MP4 export\n' >&2
    exit 127
fi

seconds="${XPOST_SECONDS:-30}"
fps="${XPOST_FPS:-30}"
width="${XPOST_WIDTH:-1280}"
height="${XPOST_HEIGHT:-720}"
frames="${XPOST_FRAMES:-$((seconds * fps))}"

if (( width % 2 != 0 || height % 2 != 0 )); then
    printf 'XPOST_WIDTH and XPOST_HEIGHT must be even for yuv420p MP4 output\n' >&2
    exit 2
fi

if (( $# > 0 )); then
    bin="$1"
    if [[ "$bin" != /* ]]; then
        bin="$old_pwd/$bin"
    fi
else
    bin="$repo_dir/target/release/realtimerays"
    if [[ "${XPOST_BUILD:-1}" != "0" || ! -x "$bin" ]]; then
        cargo build --release
    fi
fi

if [[ ! -x "$bin" ]]; then
    printf 'Renderer binary is not executable: %s\n' "$bin" >&2
    exit 126
fi

out_dir="${XPOST_DIR:-target/xpost}"
mkdir -p "$out_dir"
stem="$out_dir/realtimerays-$(date +%Y%m%d-%H%M%S)"
mp4_out="${XPOST_OUT:-$stem.mp4}"
stats_out="${XPOST_STATS:-$stem.stats.txt}"
icd="${VULKAN_SDK:-}/share/vulkan/icd.d/MoltenVK_icd.json"

printf 'Rendering %ux%u %s frames @ %s fps to %s\n' "$width" "$height" "$frames" "$fps" "$mp4_out" | tee "$stats_out"

{
    if [[ -z "${VK_ICD_FILENAMES:-}" && -f "$icd" ]]; then
        VK_ICD_FILENAMES="$icd" "$bin" --xpost-raw "$width" "$height" "$frames" "$fps"
    else
        "$bin" --xpost-raw "$width" "$height" "$frames" "$fps"
    fi
} 2> >(tee -a "$stats_out" >&2) | ffmpeg -hide_banner -loglevel error -y \
    -f rawvideo -pix_fmt rgba -s "${width}x${height}" -r "$fps" -i - \
    -c:v libx264 -profile:v high -level 4.0 -preset "${XPOST_PRESET:-slow}" -crf "${XPOST_CRF:-20}" \
    -pix_fmt yuv420p \
    -tag:v avc1 \
    -movflags +faststart \
    -an \
    "$mp4_out" 2> >(tee -a "$stats_out" >&2)

mp4_bytes="$(wc -c < "$mp4_out" | tr -d ' ')"
{
    printf 'mp4_out=%s\n' "$mp4_out"
    printf 'mp4_bytes=%s\n' "$mp4_bytes"
    if command -v ffprobe >/dev/null 2>&1; then
        ffprobe -v error -select_streams v:0 \
            -show_entries stream=codec_name,profile,width,height,pix_fmt,r_frame_rate:format=duration,size,bit_rate \
            -of default=noprint_wrappers=1 "$mp4_out"
    fi
    printf 'stats_out=%s\n' "$stats_out"
} | tee -a "$stats_out"
