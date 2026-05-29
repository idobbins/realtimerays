#!/usr/bin/env python3
import argparse
import json
from pathlib import Path

import numpy as np
import torch
from PIL import Image, ImageDraw

from evaluate_denoiser import (
    FILTER_TAP_PATTERNS,
    PATTERN_BY_WEIGHT_COUNT,
    RGB_CNN_LAYOUTS,
    RGB_CNN_MODES,
    infer_rgb_cnn_kernel_size,
    learned_filter,
    load_crop,
    parse_weight_arg,
    rgb_cnn,
)


def to_srgb8(tensor: torch.Tensor) -> Image.Image:
    image = tensor.detach().cpu().clamp(0.0, 1.0).squeeze(0).permute(1, 2, 0).numpy()
    image = np.power(image, 1.0 / 2.2)
    return Image.fromarray((image * 255.0 + 0.5).astype(np.uint8), mode="RGB")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dataset", type=Path)
    parser.add_argument("--weights", action="append", default=[], help="name=path learned-filter or rgb-cnn weights")
    parser.add_argument("--output", type=Path, default=Path("renders/denoiser_sparse13_comparison"))
    parser.add_argument("--frame", type=int)
    parser.add_argument("--crop-size", type=int, default=256)
    parser.add_argument("--x", type=int)
    parser.add_argument("--y", type=int)
    parser.add_argument("--rgb-cnn-layout", choices=RGB_CNN_LAYOUTS, default="dense")
    parser.add_argument("--rgb-cnn-mode", choices=RGB_CNN_MODES, default="rgb")
    parser.add_argument("--rgb-cnn-kernel-size", type=int)
    parser.add_argument("--rgb-cnn-dilation", type=int, default=4)
    parser.add_argument("--device", default="mps" if torch.backends.mps.is_available() else "cpu")
    args = parser.parse_args()

    with (args.dataset / "dataset_meta.json").open("r", encoding="utf-8") as f:
        meta = json.load(f)
    width = int(meta["width"])
    height = int(meta["height"])
    frame_count = int(meta["frames"])
    frame = args.frame if args.frame is not None else max(0, frame_count - 1)
    x = args.x if args.x is not None else (width - args.crop_size) // 2
    y = args.y if args.y is not None else (height - args.crop_size) // 2

    candidates = []
    needs_guides = False
    for item in args.weights:
        name, path = parse_weight_arg(item)
        raw = np.fromfile(path, dtype="<f4")
        pattern_name = PATTERN_BY_WEIGHT_COUNT.get(raw.size)
        if pattern_name is not None:
            weights = torch.from_numpy(raw.astype(np.float32)).to(args.device)
            candidates.append((name, "filter", weights, FILTER_TAP_PATTERNS[pattern_name]))
            needs_guides = True
        elif args.rgb_cnn_layout != "dense" or infer_rgb_cnn_kernel_size(raw.size, args.rgb_cnn_mode) is not None:
            weights = torch.from_numpy(raw.astype(np.float32)).to(args.device)
            candidates.append((name, "rgb-cnn", weights, None))
        else:
            expected = ", ".join(f"{6 + len(taps)} ({name})" for name, taps in FILTER_TAP_PATTERNS.items())
            raise ValueError(
                f"{path} has {raw.size} floats, expected one of: {expected}, "
                "or rgb-cnn weights with an odd-square kernel footprint"
            )
    channels = set(meta.get("channels", []))
    has_guides = {"input_normal", "input_albedo", "input_depth"}.issubset(channels)
    if needs_guides and not has_guides:
        raise ValueError(f"{args.dataset} does not contain guide channels for learned-filter weights")

    inputs, target = load_crop(args.dataset, frame, x, y, args.crop_size, width, height, load_guides=needs_guides)
    inputs = inputs.to(args.device)
    target = target.to(args.device)

    columns: list[tuple[str, Image.Image]] = [
        ("noisy", to_srgb8(inputs[:, 0:3])),
    ]
    for name, kind, weights, tap_offsets in candidates:
        if kind == "rgb-cnn":
            pred = rgb_cnn(
                inputs,
                weights,
                args.rgb_cnn_kernel_size,
                args.rgb_cnn_layout,
                args.rgb_cnn_dilation,
                args.rgb_cnn_mode,
            )
        else:
            pred = learned_filter(inputs, weights, tap_offsets)
        columns.append((name, to_srgb8(pred)))
    columns.append(("target", to_srgb8(target)))

    label_height = 24
    tile_w, tile_h = columns[0][1].size
    sheet = Image.new("RGB", (tile_w * len(columns), tile_h + label_height), (20, 20, 20))
    draw = ImageDraw.Draw(sheet)
    for index, (label, image) in enumerate(columns):
        x0 = index * tile_w
        sheet.paste(image, (x0, label_height))
        draw.text((x0 + 6, 5), label, fill=(235, 235, 235))

    args.output.mkdir(parents=True, exist_ok=True)
    path = args.output / f"comparison_frame_{frame:06}_x{x}_y{y}.png"
    sheet.save(path)
    print(f"wrote {path}")


if __name__ == "__main__":
    main()
