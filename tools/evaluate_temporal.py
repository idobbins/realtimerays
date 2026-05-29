#!/usr/bin/env python3
import argparse
import json
from pathlib import Path

import numpy as np
import torch
from PIL import Image, ImageDraw

try:
    import lpips
except ImportError as exc:
    raise SystemExit("lpips is required; install tools/requirements-denoiser.txt") from exc


RECORD_FPS = 60.0
MAX_RAY_DISTANCE = 80.0
CAMERA_ORBIT_SPEED = 0.12
CAMERA_ORBIT_ANGLE = 0.45
CAMERA_ORBIT_RADIUS = 34.0
CAMERA_HEIGHT = 13.0
CAMERA_FOCAL_LENGTH = 1.35


def load_rgba16f(root: Path, name: str, frame: int, height: int, width: int) -> np.ndarray:
    path = root / f"{name}_{frame:06}.rgba16f"
    return np.memmap(path, dtype=np.float16, mode="r").reshape(height, width, 4)


def normalize(v: np.ndarray) -> np.ndarray:
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1.0e-8)


def camera_at(frame: int) -> dict[str, np.ndarray | float]:
    time = frame / RECORD_FPS
    a = CAMERA_ORBIT_ANGLE + time * CAMERA_ORBIT_SPEED
    target = np.array([0.0, 2.25, 0.0], dtype=np.float32)
    ro = np.array(
        [
            target[0] + np.sin(a) * CAMERA_ORBIT_RADIUS,
            target[1] + CAMERA_HEIGHT,
            target[2] + np.cos(a) * CAMERA_ORBIT_RADIUS,
        ],
        dtype=np.float32,
    )
    forward = target - ro
    forward = forward / np.linalg.norm(forward)
    world_up = np.array([0.0, 1.0, 0.0], dtype=np.float32)
    right = np.cross(forward, world_up)
    right = right / np.linalg.norm(right)
    up = np.cross(right, forward)
    return {
        "ro": ro,
        "right": right.astype(np.float32),
        "up": up.astype(np.float32),
        "forward": forward.astype(np.float32),
        "focal": CAMERA_FOCAL_LENGTH,
    }


def pixel_grid(width: int, height: int) -> tuple[np.ndarray, np.ndarray]:
    y, x = np.mgrid[0:height, 0:width]
    return x.astype(np.float32), y.astype(np.float32)


def camera_rays(cam: dict[str, np.ndarray | float], width: int, height: int) -> np.ndarray:
    x, y = pixel_grid(width, height)
    uv_x = ((x + 0.5) / width) * 2.0 - 1.0
    uv_x *= width / height
    uv_y = -(((y + 0.5) / height) * 2.0 - 1.0)
    rays = (
        cam["right"][None, None, :] * uv_x[..., None]
        + cam["up"][None, None, :] * uv_y[..., None]
        + cam["forward"][None, None, :] * float(cam["focal"])
    )
    return normalize(rays).astype(np.float32)


def project_points(
    points: np.ndarray,
    cam: dict[str, np.ndarray | float],
    width: int,
    height: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    v = points - cam["ro"][None, None, :]
    distance = np.linalg.norm(v, axis=-1)
    d = v / np.maximum(distance[..., None], 1.0e-8)
    denom = np.sum(d * cam["forward"][None, None, :], axis=-1)
    uv_x = float(cam["focal"]) * np.sum(d * cam["right"][None, None, :], axis=-1) / np.maximum(denom, 1.0e-8)
    uv_y = float(cam["focal"]) * np.sum(d * cam["up"][None, None, :], axis=-1) / np.maximum(denom, 1.0e-8)
    px = ((uv_x / (width / height)) + 1.0) * 0.5 * width - 0.5
    py = ((-uv_y) + 1.0) * 0.5 * height - 0.5
    valid = (denom > 1.0e-5) & (px >= 0.0) & (px <= width - 1.0) & (py >= 0.0) & (py <= height - 1.0)
    return px.astype(np.float32), py.astype(np.float32), (distance / MAX_RAY_DISTANCE).astype(np.float32), valid


def bilinear_sample(image: np.ndarray, x: np.ndarray, y: np.ndarray) -> np.ndarray:
    height, width = image.shape[:2]
    x = np.clip(x, 0.0, width - 1.0)
    y = np.clip(y, 0.0, height - 1.0)
    x0 = np.floor(x).astype(np.int32)
    y0 = np.floor(y).astype(np.int32)
    x1 = np.minimum(x0 + 1, width - 1)
    y1 = np.minimum(y0 + 1, height - 1)
    wx = (x - x0).astype(np.float32)
    wy = (y - y0).astype(np.float32)

    top = image[y0, x0] * (1.0 - wx[..., None]) + image[y0, x1] * wx[..., None]
    bottom = image[y1, x0] * (1.0 - wx[..., None]) + image[y1, x1] * wx[..., None]
    return top * (1.0 - wy[..., None]) + bottom * wy[..., None]


def bilinear_sample_scalar(image: np.ndarray, x: np.ndarray, y: np.ndarray) -> np.ndarray:
    return bilinear_sample(image[..., None], x, y)[..., 0]


def local_bounds(color: np.ndarray, radius: int, margin: float) -> tuple[np.ndarray, np.ndarray]:
    if radius <= 0:
        return np.maximum(color - margin, 0.0), np.minimum(color + margin, 1.0)
    tensor = torch.from_numpy(color).permute(2, 0, 1).unsqueeze(0)
    kernel = radius * 2 + 1
    local_max = torch.nn.functional.max_pool2d(tensor, kernel, stride=1, padding=radius)
    local_min = -torch.nn.functional.max_pool2d(-tensor, kernel, stride=1, padding=radius)
    lo = local_min.squeeze(0).permute(1, 2, 0).numpy() - margin
    hi = local_max.squeeze(0).permute(1, 2, 0).numpy() + margin
    return np.maximum(lo, 0.0), np.minimum(hi, 1.0)


def decode_normal(encoded: np.ndarray) -> np.ndarray:
    n = encoded * 2.0 - 1.0
    return normalize(n)


def temporal_step(
    frame: int,
    current_color: np.ndarray,
    current_normal: np.ndarray,
    current_albedo: np.ndarray,
    current_depth: np.ndarray,
    prev: dict[str, np.ndarray] | None,
    args: argparse.Namespace,
) -> tuple[np.ndarray, dict[str, np.ndarray]]:
    if prev is None:
        history = {
            "color": current_color,
            "normal": current_normal,
            "albedo": current_albedo,
            "depth": current_depth,
            "count": np.ones(current_depth.shape, dtype=np.float32),
            "frame": np.array(frame, dtype=np.int32),
        }
        return current_color, history

    height, width = current_depth.shape
    curr_cam = camera_at(frame)
    prev_cam = camera_at(int(prev["frame"]))
    rays = camera_rays(curr_cam, width, height)
    points = curr_cam["ro"][None, None, :] + rays * (current_depth * MAX_RAY_DISTANCE)[..., None]
    prev_x, prev_y, expected_prev_depth, projected = project_points(points, prev_cam, width, height)

    prev_color = bilinear_sample(prev["color"], prev_x, prev_y)
    prev_normal = bilinear_sample(prev["normal"], prev_x, prev_y)
    prev_albedo = bilinear_sample(prev["albedo"], prev_x, prev_y)
    prev_depth = bilinear_sample_scalar(prev["depth"], prev_x, prev_y)
    prev_count = bilinear_sample_scalar(prev["count"], prev_x, prev_y)

    current_surface = current_depth < args.sky_depth
    depth_ok = np.abs(prev_depth - expected_prev_depth) < args.depth_threshold
    normal_dot = np.sum(decode_normal(current_normal) * decode_normal(prev_normal), axis=-1)
    normal_ok = normal_dot > args.normal_threshold
    albedo_ok = np.mean(np.abs(prev_albedo - current_albedo), axis=-1) < args.albedo_threshold

    grid_x, grid_y = pixel_grid(width, height)
    motion = np.sqrt((prev_x - grid_x) ** 2 + (prev_y - grid_y) ** 2)
    motion_conf = np.exp(-args.motion_decay * motion)

    valid = projected & current_surface & depth_ok & normal_ok & albedo_ok
    confidence = motion_conf * valid.astype(np.float32)
    history_weight = confidence * np.minimum(prev_count, args.max_history) / (
        np.minimum(prev_count, args.max_history) + 1.0
    )
    history_weight = np.minimum(history_weight, args.max_history_weight)

    lo, hi = local_bounds(current_color, args.clamp_radius, args.clamp_margin)
    prev_color = np.minimum(np.maximum(prev_color, lo), hi)
    output = current_color * (1.0 - history_weight[..., None]) + prev_color * history_weight[..., None]
    output = np.clip(output, 0.0, 1.0).astype(np.float32)

    next_count = 1.0 + confidence * np.minimum(prev_count, args.max_history - 1.0)
    history = {
        "color": output,
        "normal": current_normal,
        "albedo": current_albedo,
        "depth": current_depth,
        "count": np.clip(next_count, 1.0, args.max_history).astype(np.float32),
        "frame": np.array(frame, dtype=np.int32),
    }
    return output, history


def crop_locations(width: int, height: int, crop_size: int, count: int) -> list[tuple[int, int]]:
    x_max = max(0, width - crop_size)
    y_max = max(0, height - crop_size)
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


def to_tensor(image: np.ndarray, device: str) -> torch.Tensor:
    return torch.from_numpy(image).permute(2, 0, 1).unsqueeze(0).to(device)


def to_srgb8(image: np.ndarray) -> Image.Image:
    srgb = np.power(np.clip(image, 0.0, 1.0), 1.0 / 2.2)
    return Image.fromarray((srgb * 255.0 + 0.5).astype(np.uint8), mode="RGB")


def save_comparison(path: Path, columns: list[tuple[str, np.ndarray]]) -> None:
    images = [(label, to_srgb8(image)) for label, image in columns]
    label_height = 24
    tile_w, tile_h = images[0][1].size
    sheet = Image.new("RGB", (tile_w * len(images), tile_h + label_height), (20, 20, 20))
    draw = ImageDraw.Draw(sheet)
    for index, (label, image) in enumerate(images):
        x0 = index * tile_w
        sheet.paste(image, (x0, label_height))
        draw.text((x0 + 6, 5), label, fill=(235, 235, 235))
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path)


def add_metrics(values: dict[str, list[float]], pred: np.ndarray, target: np.ndarray, prefix: str) -> None:
    error = pred - target
    values[f"{prefix}_l1"].append(float(np.mean(np.abs(error))))
    values[f"{prefix}_mse"].append(float(np.mean(error * error)))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dataset", type=Path)
    parser.add_argument("--frames", type=int)
    parser.add_argument("--max-history", type=float, default=4.0)
    parser.add_argument("--max-history-weight", type=float, default=0.85)
    parser.add_argument("--motion-decay", type=float, default=0.12)
    parser.add_argument("--depth-threshold", type=float, default=0.01)
    parser.add_argument("--normal-threshold", type=float, default=0.75)
    parser.add_argument("--albedo-threshold", type=float, default=0.15)
    parser.add_argument("--sky-depth", type=float, default=0.999)
    parser.add_argument("--clamp-radius", type=int, default=1)
    parser.add_argument("--clamp-margin", type=float, default=0.03)
    parser.add_argument("--crop-size", type=int, default=256)
    parser.add_argument("--crops-per-frame", type=int, default=4)
    parser.add_argument("--lpips-net", choices=["alex", "vgg", "squeeze"], default="squeeze")
    parser.add_argument("--device", default="mps" if torch.backends.mps.is_available() else "cpu")
    parser.add_argument("--output", type=Path, default=Path("renders/temporal_comparison"))
    args = parser.parse_args()

    with (args.dataset / "dataset_meta.json").open("r", encoding="utf-8") as f:
        meta = json.load(f)
    width = int(meta["width"])
    height = int(meta["height"])
    frame_count = min(int(meta["frames"]), args.frames or int(meta["frames"]))
    channels = set(meta.get("channels", []))
    required = {"input_color", "input_normal", "input_albedo", "input_depth", "target_color"}
    missing = required - channels
    if missing:
        raise ValueError(f"{args.dataset} missing required channels: {', '.join(sorted(missing))}")

    val_count = max(1, frame_count // 5)
    val_frames = set(range(max(0, frame_count - val_count), frame_count))
    locations = crop_locations(width, height, args.crop_size, args.crops_per_frame)

    lpips_model = lpips.LPIPS(net=args.lpips_net).to(args.device).eval()
    for param in lpips_model.parameters():
        param.requires_grad_(False)

    metrics: dict[str, list[float]] = {
        "noisy_l1": [],
        "noisy_mse": [],
        "temporal_l1": [],
        "temporal_mse": [],
        "noisy_lpips": [],
        "temporal_lpips": [],
    }
    prev = None
    last_comparison: tuple[int, int, int, np.ndarray, np.ndarray, np.ndarray] | None = None

    with torch.no_grad():
        for frame in range(frame_count):
            color = load_rgba16f(args.dataset, "input_color", frame, height, width)[..., 0:3].astype(np.float32)
            normal = load_rgba16f(args.dataset, "input_normal", frame, height, width)[..., 0:3].astype(np.float32)
            albedo = load_rgba16f(args.dataset, "input_albedo", frame, height, width)[..., 0:3].astype(np.float32)
            depth = load_rgba16f(args.dataset, "input_depth", frame, height, width)[..., 0].astype(np.float32)
            target = load_rgba16f(args.dataset, "target_color", frame, height, width)[..., 0:3].astype(np.float32)

            temporal, prev = temporal_step(frame, color, normal, albedo, depth, prev, args)

            if frame not in val_frames:
                continue

            add_metrics(metrics, color, target, "noisy")
            add_metrics(metrics, temporal, target, "temporal")

            for x0, y0 in locations:
                crop = np.s_[y0 : y0 + args.crop_size, x0 : x0 + args.crop_size]
                target_t = to_tensor(target[crop], args.device)
                noisy_t = to_tensor(color[crop], args.device)
                temporal_t = to_tensor(temporal[crop], args.device)
                metrics["noisy_lpips"].append(float(lpips_model(noisy_t * 2.0 - 1.0, target_t * 2.0 - 1.0).cpu()))
                metrics["temporal_lpips"].append(
                    float(lpips_model(temporal_t * 2.0 - 1.0, target_t * 2.0 - 1.0).cpu())
                )
                last_comparison = (frame, x0, y0, color[crop], temporal[crop], target[crop])

    print("name,lpips,l1,rmse,samples")
    for name in ("noisy", "temporal"):
        lpips_mean = sum(metrics[f"{name}_lpips"]) / len(metrics[f"{name}_lpips"])
        l1_mean = sum(metrics[f"{name}_l1"]) / len(metrics[f"{name}_l1"])
        rmse = (sum(metrics[f"{name}_mse"]) / len(metrics[f"{name}_mse"])) ** 0.5
        print(f"{name},{lpips_mean:.6f},{l1_mean:.6f},{rmse:.6f},{len(metrics[f'{name}_lpips'])}")

    if last_comparison is not None:
        frame, x0, y0, noisy, temporal, target = last_comparison
        path = args.output / f"temporal_frame_{frame:06}_x{x0}_y{y0}.png"
        save_comparison(path, [("noisy", noisy), ("temporal", temporal), ("target", target)])
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
