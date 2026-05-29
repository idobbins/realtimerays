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
RECON_CNN_INPUT_CHANNELS = 4
RGB_CNN_OUTPUT_CHANNELS = 3
RGB_CNN_DEFAULT_KERNEL_SIZE = 3
MAX_CNN_RGB_DELTA = 0.22
RGB_CNN_LAYOUTS = ("dense", "sparse-wide", "dilated3", "dilated5", "axis17")
RGB_CNN_MODES = ("rgb", "luma", "recon")
RGB_CNN_CHROMA_EPSILON = 0.0001
RGB_CNN_CHROMA_FALLBACK_LUMA = 0.01
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


def rgb_cnn_tap_offsets(layout: str, kernel_size: int = 3, dilation: int = 4) -> list[tuple[int, int]]:
    if layout == "dense":
        radius = kernel_size // 2
        return [(x, y) for y in range(-radius, radius + 1) for x in range(-radius, radius + 1)]
    if layout == "sparse-wide":
        offsets = [(x, y) for y in range(-2, 3) for x in range(-2, 3)]
        for radius in (4, 8, 12):
            offsets.extend(
                [
                    (radius, 0),
                    (-radius, 0),
                    (0, radius),
                    (0, -radius),
                    (radius, radius),
                    (-radius, radius),
                    (radius, -radius),
                    (-radius, -radius),
                ]
            )
        return offsets
    if layout in {"dilated3", "dilated5"}:
        size = 3 if layout == "dilated3" else 5
        radius = size // 2
        return [
            (x * dilation, y * dilation)
            for y in range(-radius, radius + 1)
            for x in range(-radius, radius + 1)
        ]
    if layout == "axis17":
        radius = 8
        return [(x, 0) for x in range(-radius, radius + 1)] + [
            (0, y) for y in range(-radius, radius + 1)
        ]
    raise ValueError(f"unknown rgb-cnn layout: {layout}")


def rgb_cnn_channels(mode: str) -> tuple[int, int]:
    if mode == "rgb":
        return RGB_CNN_INPUT_CHANNELS, RGB_CNN_OUTPUT_CHANNELS
    if mode == "luma":
        return 1, 1
    if mode == "recon":
        return RECON_CNN_INPUT_CHANNELS, RGB_CNN_OUTPUT_CHANNELS
    raise ValueError(f"unknown rgb-cnn mode: {mode}")


def rgb_cnn_weight_count(kernel_size: int, layout: str = "dense", dilation: int = 4, mode: str = "rgb") -> int:
    tap_count = len(rgb_cnn_tap_offsets(layout, kernel_size, dilation))
    input_channels, output_channels = rgb_cnn_channels(mode)
    return (
        RGB_CNN_HIDDEN_CHANNELS * input_channels * tap_count
        + RGB_CNN_HIDDEN_CHANNELS
        + output_channels * RGB_CNN_HIDDEN_CHANNELS
        + output_channels
    )


RGB_CNN_WEIGHT_COUNT = rgb_cnn_weight_count(RGB_CNN_DEFAULT_KERNEL_SIZE)


def infer_rgb_cnn_kernel_size(weight_count: int, mode: str = "rgb") -> int | None:
    input_channels, output_channels = rgb_cnn_channels(mode)
    head_count = RGB_CNN_HIDDEN_CHANNELS + output_channels * RGB_CNN_HIDDEN_CHANNELS + output_channels
    conv0_values = weight_count - head_count
    conv0_scale = RGB_CNN_HIDDEN_CHANNELS * input_channels
    if conv0_values <= 0 or conv0_values % conv0_scale != 0:
        return None
    taps = conv0_values // conv0_scale
    kernel_size = int(taps**0.5)
    if kernel_size * kernel_size != taps or kernel_size % 2 == 0:
        return None
    return kernel_size


def sample_rgb_offsets(color: torch.Tensor, offsets: list[tuple[int, int]]) -> torch.Tensor:
    radius = max(max(abs(x), abs(y)) for x, y in offsets)
    _, _, h, w = color.shape
    padded = torch.nn.functional.pad(color, (radius, radius, radius, radius), mode="replicate")
    samples = [
        padded[:, :, y + radius : y + radius + h, x + radius : x + radius + w]
        for x, y in offsets
    ]
    return torch.stack(samples, dim=2)


def chroma_preserving_luma(color: torch.Tensor, out_luma: torch.Tensor, fallback_offsets: list[tuple[int, int]]) -> torch.Tensor:
    center_luma = luminance(color)
    center_chroma = color / torch.clamp(center_luma, min=RGB_CNN_CHROMA_EPSILON)

    samples = sample_rgb_offsets(color, fallback_offsets)
    local_color = samples.mean(dim=2)
    local_luma = luminance(local_color)
    local_chroma = local_color / torch.clamp(local_luma, min=RGB_CNN_CHROMA_EPSILON)

    use_local = center_luma < RGB_CNN_CHROMA_FALLBACK_LUMA
    chroma = torch.where(use_local, local_chroma, center_chroma)
    return torch.clamp(chroma * out_luma, 0.0, 1.0)


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
    load_coverage: bool = False,
    sparse_coverage: float = 1.0,
):
    crop = np.s_[y0 : y0 + crop_size, x0 : x0 + crop_size]
    input_rgba = load_rgba16f(root, "input_color", frame, height, width)[crop].astype(np.float32)
    color = input_rgba[..., 0:3]
    target = load_rgba16f(root, "target_color", frame, height, width)[crop][..., 0:3].astype(np.float32)
    if load_coverage:
        coverage = (input_rgba[..., 3:4] > 0.0).astype(np.float32)
        if sparse_coverage < 1.0:
            seed = (
                (frame + 1) * 73856093
                ^ (x0 + 1) * 19349663
                ^ (y0 + 1) * 83492791
                ^ crop_size * 2654435761
            ) & 0xFFFFFFFF
            rng = np.random.default_rng(seed)
            coverage *= (rng.random((crop_size, crop_size, 1)) < sparse_coverage).astype(np.float32)
        color = color * coverage
    if load_guides:
        normal = load_rgba16f(root, "input_normal", frame, height, width)[crop][..., 0:3].astype(np.float32)
        albedo = load_rgba16f(root, "input_albedo", frame, height, width)[crop][..., 0:3].astype(np.float32)
        depth = load_rgba16f(root, "input_depth", frame, height, width)[crop][..., 0:1].astype(np.float32)
        parts = [color, normal, albedo, depth]
        if load_coverage:
            parts.append(coverage)
        inputs = np.concatenate(parts, axis=2)
    elif load_coverage:
        inputs = np.concatenate([color, coverage], axis=2)
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


def rgb_cnn(
    inputs: torch.Tensor,
    weights: torch.Tensor,
    kernel_size: int | None = None,
    layout: str = "dense",
    dilation: int = 4,
    mode: str = "rgb",
) -> torch.Tensor:
    color = inputs[:, 0:3]
    input_channels, output_channels = rgb_cnn_channels(mode)
    if layout == "dense" and kernel_size is None:
        kernel_size = infer_rgb_cnn_kernel_size(weights.numel(), mode)
    if layout == "dense" and kernel_size is None:
        raise ValueError(f"cannot infer rgb-cnn kernel size from {weights.numel()} f32 values")
    if kernel_size is None:
        kernel_size = RGB_CNN_DEFAULT_KERNEL_SIZE
    tap_offsets = rgb_cnn_tap_offsets(layout, kernel_size, dilation)
    tap_count = len(tap_offsets)
    conv0_size = RGB_CNN_HIDDEN_CHANNELS * input_channels * tap_count
    conv0_bias_offset = conv0_size
    conv1_weight_offset = conv0_bias_offset + RGB_CNN_HIDDEN_CHANNELS
    conv1_bias_offset = conv1_weight_offset + output_channels * RGB_CNN_HIDDEN_CHANNELS
    expected_count = conv1_bias_offset + output_channels
    if weights.numel() != expected_count:
        raise ValueError(
            f"rgb-cnn mode={mode} layout={layout} kernel_size={kernel_size} dilation={dilation} "
            f"expects {expected_count} f32 values, found {weights.numel()}"
        )

    conv0_weight = weights[:conv0_size].view(RGB_CNN_HIDDEN_CHANNELS, input_channels, tap_count)
    conv0_bias = weights[conv0_bias_offset:conv1_weight_offset]
    conv1_weight = weights[conv1_weight_offset:conv1_bias_offset].view(output_channels, RGB_CNN_HIDDEN_CHANNELS, 1, 1)
    conv1_bias = weights[conv1_bias_offset:expected_count]

    if mode == "luma":
        conv_input = luminance(color)
    elif mode == "recon":
        coverage = inputs[:, 10:11] if inputs.shape[1] > 10 else inputs[:, 3:4]
        conv_input = torch.cat([color, coverage], dim=1)
    else:
        conv_input = color
    if layout == "dense":
        radius = kernel_size // 2
        padded = torch.nn.functional.pad(conv_input, (radius, radius, radius, radius), mode="replicate")
        hidden = torch.nn.functional.relu(
            torch.nn.functional.conv2d(
                padded,
                conv0_weight.view(RGB_CNN_HIDDEN_CHANNELS, input_channels, kernel_size, kernel_size),
                conv0_bias,
            )
        )
    else:
        samples = sample_rgb_offsets(conv_input, tap_offsets)
        hidden = torch.einsum("hct,bctxy->bhxy", conv0_weight, samples)
        hidden = torch.nn.functional.relu(hidden + conv0_bias.view(1, RGB_CNN_HIDDEN_CHANNELS, 1, 1))
    raw_delta = torch.nn.functional.conv2d(hidden, conv1_weight, conv1_bias)
    if mode == "luma":
        delta_luma = MAX_CNN_RGB_DELTA * torch.tanh(raw_delta)
        out_luma = torch.clamp(luminance(color) + delta_luma, 0.0, 1.0)
        return chroma_preserving_luma(color, out_luma, tap_offsets)
    if mode == "recon":
        return torch.sigmoid(raw_delta)
    delta_rgb = MAX_CNN_RGB_DELTA * torch.tanh(raw_delta)
    return torch.clamp(color + delta_rgb, 0.0, 1.0)


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
    parser.add_argument("--rgb-cnn-layout", choices=RGB_CNN_LAYOUTS, default="dense")
    parser.add_argument("--rgb-cnn-mode", choices=RGB_CNN_MODES, default="rgb")
    parser.add_argument("--rgb-cnn-kernel-size", type=int)
    parser.add_argument("--rgb-cnn-dilation", type=int, default=4)
    parser.add_argument("--sparse-coverage", type=float, default=1.0)
    parser.add_argument("--device", default="mps" if torch.backends.mps.is_available() else "cpu")
    args = parser.parse_args()
    if not (0.0 < args.sparse_coverage <= 1.0):
        raise ValueError("--sparse-coverage must be > 0 and <= 1")

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
        elif args.rgb_cnn_layout != "dense" or infer_rgb_cnn_kernel_size(raw.size, args.rgb_cnn_mode) is not None:
            candidates.append((name, "rgb-cnn", torch.from_numpy(raw.astype(np.float32)).to(args.device), None))
        else:
            expected = ", ".join(f"{6 + len(taps)} ({name})" for name, taps in FILTER_TAP_PATTERNS.items())
            raise ValueError(
                f"{path} has {raw.size} floats, expected one of: {expected}, "
                "or rgb-cnn weights with an odd-square kernel footprint"
            )
    needs_guides = any(kind == "filter" for _, kind, _, _ in candidates)
    needs_coverage = args.rgb_cnn_mode == "recon" or args.sparse_coverage < 1.0
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
                        load_coverage=needs_coverage,
                        sparse_coverage=args.sparse_coverage,
                    )
                    inputs = inputs.to(args.device)
                    targets = targets.to(args.device)
                    if kind == "noisy":
                        pred = inputs[:, 0:3]
                    elif kind == "rgb-cnn":
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
                    lpips_values.append(float(lpips_model(pred * 2.0 - 1.0, targets * 2.0 - 1.0).cpu()))
                    l1_values.append(float(torch.mean(torch.abs(pred - targets)).cpu()))
                    mse_values.append(float(torch.mean((pred - targets) ** 2).cpu()))
        lpips_mean = sum(lpips_values) / len(lpips_values)
        l1_mean = sum(l1_values) / len(l1_values)
        rmse = (sum(mse_values) / len(mse_values)) ** 0.5
        print(f"{name},{lpips_mean:.6f},{l1_mean:.6f},{rmse:.6f},{len(lpips_values)}")


if __name__ == "__main__":
    main()
