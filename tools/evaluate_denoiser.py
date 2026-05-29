#!/usr/bin/env python3
import argparse
import json
from pathlib import Path

import numpy as np
import torch

try:
    import lpips
except ImportError as exc:
    raise SystemExit("lpips is required; install tools/requirements-denoiser.txt") from exc


MAX_FILTER_RADIUS = 4
MAX_FILTER_SIZE = MAX_FILTER_RADIUS * 2 + 1
DISABLED_PENALTY = -10.0
MAX_LUMA_BLEND = 0.45
RGB_CNN_HIDDEN_CHANNELS = 8
RGB_CNN_INPUT_CHANNELS = 3
RGB_CNN_WEIGHT_COUNT = 251
MAX_CNN_RGB_DELTA = 0.22
LUMA_WEIGHTS = torch.tensor([0.2126, 0.7152, 0.0722], dtype=torch.float32).view(1, 3, 1, 1)
FILTER_TAP_PATTERNS = {
    "dense3": [
        (-1, -1),
        (0, -1),
        (1, -1),
        (-1, 0),
        (0, 0),
        (1, 0),
        (-1, 1),
        (0, 1),
        (1, 1),
    ],
    "sparse13": [
        (-1, -1),
        (0, -1),
        (1, -1),
        (-1, 0),
        (0, 0),
        (1, 0),
        (-1, 1),
        (0, 1),
        (1, 1),
        (-4, 0),
        (4, 0),
        (0, -4),
        (0, 4),
    ],
}
PATTERN_BY_WEIGHT_COUNT = {6 + len(taps): name for name, taps in FILTER_TAP_PATTERNS.items()}


def luminance(rgb: torch.Tensor) -> torch.Tensor:
    weights = LUMA_WEIGHTS.to(device=rgb.device, dtype=rgb.dtype)
    return torch.sum(rgb * weights, dim=1, keepdim=True)


def load_rgba16f(root: Path, name: str, frame: int, height: int, width: int) -> np.ndarray:
    path = root / f"{name}_{frame:06}.rgba16f"
    return np.memmap(path, dtype=np.float16, mode="r").reshape(height, width, 4)


def crop_locations(width: int, height: int, crop_size: int, count: int) -> list[tuple[int, int]]:
    x_max = width - crop_size
    y_max = height - crop_size
    candidates = [
        (x_max // 2, y_max // 2),
        (0, 0),
        (x_max, 0),
        (0, y_max),
        (x_max, y_max),
        (x_max // 4, y_max // 3),
        ((3 * x_max) // 4, (2 * y_max) // 3),
        (x_max // 2, y_max),
    ]
    return candidates[:count]


def load_crop(
    root: Path,
    frame: int,
    x0: int,
    y0: int,
    crop_size: int,
    width: int,
    height: int,
    load_guides: bool = True,
):
    crop = np.s_[y0 : y0 + crop_size, x0 : x0 + crop_size]
    color = load_rgba16f(root, "input_color", frame, height, width)[crop][..., 0:3].astype(np.float32)
    target = load_rgba16f(root, "target_color", frame, height, width)[crop][..., 0:3].astype(np.float32)
    if load_guides:
        normal = load_rgba16f(root, "input_normal", frame, height, width)[crop][..., 0:3].astype(np.float32)
        albedo = load_rgba16f(root, "input_albedo", frame, height, width)[crop][..., 0:3].astype(np.float32)
        depth = load_rgba16f(root, "input_depth", frame, height, width)[crop][..., 0:1].astype(np.float32)
        inputs = np.concatenate([color, normal, albedo, depth], axis=2)
    else:
        inputs = color
    return (
        torch.from_numpy(inputs).permute(2, 0, 1).unsqueeze(0),
        torch.from_numpy(target).permute(2, 0, 1).unsqueeze(0),
    )


def learned_filter(inputs: torch.Tensor, weights: torch.Tensor, tap_offsets: list[tuple[int, int]]) -> torch.Tensor:
    color = inputs[:, 0:3]
    normal = inputs[:, 3:6]
    albedo = inputs[:, 6:9]
    depth = inputs[:, 9:10]
    b, _, h, w = inputs.shape

    padded = torch.nn.functional.pad(
        inputs,
        (MAX_FILTER_RADIUS, MAX_FILTER_RADIUS, MAX_FILTER_RADIUS, MAX_FILTER_RADIUS),
        mode="replicate",
    )
    patches = torch.nn.functional.unfold(padded, kernel_size=MAX_FILTER_SIZE)
    patches = patches.view(b, 10, MAX_FILTER_SIZE * MAX_FILTER_SIZE, h * w)
    tap_indices = torch.tensor(
        [(y + MAX_FILTER_RADIUS) * MAX_FILTER_SIZE + x + MAX_FILTER_RADIUS for x, y in tap_offsets],
        device=inputs.device,
        dtype=torch.long,
    )
    patches = patches.index_select(2, tap_indices)
    tap_count = len(tap_offsets)

    center_color = color.reshape(b, 3, 1, h * w)
    center_normal = normal.reshape(b, 3, 1, h * w)
    center_albedo = albedo.reshape(b, 3, 1, h * w)
    center_depth = depth.reshape(b, 1, 1, h * w)

    patch_color = patches[:, 0:3]
    patch_normal = patches[:, 3:6]
    patch_albedo = patches[:, 6:9]
    patch_depth = patches[:, 9:10]

    blend = MAX_LUMA_BLEND * torch.sigmoid(weights[1])
    color_raw, normal_raw, albedo_raw, depth_raw = weights[2], weights[3], weights[4], weights[5]
    logits = weights[6:].view(1, tap_count, 1)

    if color_raw > DISABLED_PENALTY:
        logits = logits - torch.nn.functional.softplus(color_raw) * ((patch_color - center_color) ** 2).sum(dim=1)
    if normal_raw > DISABLED_PENALTY:
        logits = logits - torch.nn.functional.softplus(normal_raw) * ((patch_normal - center_normal) ** 2).sum(dim=1)
    if albedo_raw > DISABLED_PENALTY:
        logits = logits - torch.nn.functional.softplus(albedo_raw) * ((patch_albedo - center_albedo) ** 2).sum(dim=1)
    if depth_raw > DISABLED_PENALTY:
        logits = logits - torch.nn.functional.softplus(depth_raw) * ((patch_depth - center_depth) ** 2).sum(dim=1)

    filter_weights = torch.softmax(logits, dim=1).unsqueeze(1)
    luma_weights = LUMA_WEIGHTS.to(device=inputs.device, dtype=inputs.dtype).view(1, 3, 1, 1)
    patch_luma = torch.sum(patch_color * luma_weights, dim=1, keepdim=True)
    filtered_luma = (patch_luma * filter_weights).sum(dim=2).view(b, 1, h, w)
    center_luma = luminance(color)
    out_luma = torch.lerp(center_luma, filtered_luma, blend)
    chroma = color / torch.clamp(center_luma, min=0.0001)
    return torch.clamp(chroma * out_luma, 0.0, 1.0)


def rgb_cnn(inputs: torch.Tensor, weights: torch.Tensor) -> torch.Tensor:
    color = inputs[:, 0:3]
    b, _, h, w = color.shape
    conv0_size = RGB_CNN_HIDDEN_CHANNELS * RGB_CNN_INPUT_CHANNELS * 3 * 3
    conv0_bias_offset = conv0_size
    conv1_weight_offset = conv0_bias_offset + RGB_CNN_HIDDEN_CHANNELS
    conv1_bias_offset = conv1_weight_offset + 3 * RGB_CNN_HIDDEN_CHANNELS

    conv0_weight = weights[:conv0_size].view(RGB_CNN_HIDDEN_CHANNELS, RGB_CNN_INPUT_CHANNELS, 3, 3)
    conv0_bias = weights[conv0_bias_offset:conv1_weight_offset]
    conv1_weight = weights[conv1_weight_offset:conv1_bias_offset].view(3, RGB_CNN_HIDDEN_CHANNELS)
    conv1_bias = weights[conv1_bias_offset:RGB_CNN_WEIGHT_COUNT]

    padded = torch.nn.functional.pad(color, (1, 1, 1, 1), mode="replicate")
    patches = torch.nn.functional.unfold(padded, kernel_size=3).view(
        b,
        RGB_CNN_INPUT_CHANNELS,
        9,
        h * w,
    )
    hidden = torch.einsum("hct,bctn->bhn", conv0_weight.view(RGB_CNN_HIDDEN_CHANNELS, RGB_CNN_INPUT_CHANNELS, 9), patches)
    hidden = torch.nn.functional.relu(hidden + conv0_bias.view(1, RGB_CNN_HIDDEN_CHANNELS, 1))
    raw_delta = torch.einsum("oh,bhn->bon", conv1_weight, hidden)
    raw_delta = raw_delta + conv1_bias.view(1, 3, 1)
    delta = MAX_CNN_RGB_DELTA * torch.tanh(raw_delta.view(b, 3, h, w))
    return torch.clamp(color + delta, 0.0, 1.0)


def parse_weight_arg(value: str) -> tuple[str, Path]:
    if "=" not in value:
        path = Path(value)
        return path.stem, path
    name, path = value.split("=", 1)
    return name, Path(path)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dataset", type=Path)
    parser.add_argument("--weights", action="append", default=[], help="name=path learned-filter or rgb-cnn weights")
    parser.add_argument("--crop-size", type=int, default=256)
    parser.add_argument("--crops-per-frame", type=int, default=4)
    parser.add_argument("--lpips-net", choices=["alex", "vgg", "squeeze"], default="squeeze")
    parser.add_argument("--device", default="mps" if torch.backends.mps.is_available() else "cpu")
    args = parser.parse_args()

    with (args.dataset / "dataset_meta.json").open("r", encoding="utf-8") as f:
        meta = json.load(f)
    width = int(meta["width"])
    height = int(meta["height"])
    frame_count = int(meta["frames"])
    channels = set(meta.get("channels", []))
    val_count = max(1, frame_count // 5)
    val_frames = list(range(max(0, frame_count - val_count), frame_count))
    locations = crop_locations(width, height, args.crop_size, args.crops_per_frame)

    lpips_model = lpips.LPIPS(net=args.lpips_net).to(args.device).eval()
    for param in lpips_model.parameters():
        param.requires_grad_(False)

    candidates: list[tuple[str, str, torch.Tensor | None, list[tuple[int, int]] | None]] = [
        ("noisy", "noisy", None, None)
    ]
    for item in args.weights:
        name, path = parse_weight_arg(item)
        raw = np.fromfile(path, dtype="<f4")
        pattern_name = PATTERN_BY_WEIGHT_COUNT.get(raw.size)
        if pattern_name is not None:
            candidates.append((name, "filter", torch.from_numpy(raw.astype(np.float32)).to(args.device), FILTER_TAP_PATTERNS[pattern_name]))
        elif raw.size == RGB_CNN_WEIGHT_COUNT:
            candidates.append((name, "rgb-cnn", torch.from_numpy(raw.astype(np.float32)).to(args.device), None))
        else:
            expected = ", ".join(f"{6 + len(taps)} ({name})" for name, taps in FILTER_TAP_PATTERNS.items())
            raise ValueError(f"{path} has {raw.size} floats, expected one of: {expected}, {RGB_CNN_WEIGHT_COUNT} (rgb-cnn)")
    needs_guides = any(kind == "filter" for _, kind, _, _ in candidates)
    has_guides = {"input_normal", "input_albedo", "input_depth"}.issubset(channels)
    if needs_guides and not has_guides:
        raise ValueError(f"{args.dataset} does not contain guide channels for learned-filter weights")

    print("name,lpips,l1,rmse,samples")
    for name, kind, weights, tap_offsets in candidates:
        lpips_values = []
        l1_values = []
        mse_values = []
        with torch.no_grad():
            for frame in val_frames:
                for x0, y0 in locations:
                    inputs, targets = load_crop(
                        args.dataset,
                        frame,
                        x0,
                        y0,
                        args.crop_size,
                        width,
                        height,
                        load_guides=needs_guides,
                    )
                    inputs = inputs.to(args.device)
                    targets = targets.to(args.device)
                    if kind == "noisy":
                        pred = inputs[:, 0:3]
                    elif kind == "rgb-cnn":
                        pred = rgb_cnn(inputs, weights)
                    else:
                        pred = learned_filter(inputs, weights, tap_offsets)
                    lpips_values.append(float(lpips_model(pred * 2.0 - 1.0, targets * 2.0 - 1.0).cpu()))
                    l1_values.append(float(torch.mean(torch.abs(pred - targets)).cpu()))
                    mse_values.append(float(torch.mean((pred - targets) ** 2).cpu()))
        lpips_mean = sum(lpips_values) / len(lpips_values)
        l1_mean = sum(l1_values) / len(l1_values)
        rmse = (sum(mse_values) / len(mse_values)) ** 0.5
        print(f"{name},{lpips_mean:.6f},{l1_mean:.6f},{rmse:.6f},{len(lpips_values)}")


if __name__ == "__main__":
    main()
