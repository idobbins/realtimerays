#!/usr/bin/env python3
import argparse
import json
import random
from pathlib import Path

import numpy as np
import torch
from torch.nn import functional as F


MAX_FILTER_RADIUS = 4
MAX_FILTER_SIZE = MAX_FILTER_RADIUS * 2 + 1
MAX_FILTER_TAPS = MAX_FILTER_SIZE * MAX_FILTER_SIZE
DISABLED_PENALTY = -20.0
MAX_LUMA_BLEND = 0.45
LUMA_WEIGHTS = torch.tensor([0.2126, 0.7152, 0.0722], dtype=torch.float32).view(1, 3, 1, 1)


def luminance(rgb: torch.Tensor) -> torch.Tensor:
    weights = LUMA_WEIGHTS.to(device=rgb.device, dtype=rgb.dtype)
    return torch.sum(rgb * weights, dim=1, keepdim=True)


class LearnedFilter(torch.nn.Module):
    def __init__(self, radius: int, guides: set[str]) -> None:
        super().__init__()
        self.radius = radius
        self.guides = guides
        offsets = []
        for y in range(-MAX_FILTER_RADIUS, MAX_FILTER_RADIUS + 1):
            for x in range(-MAX_FILTER_RADIUS, MAX_FILTER_RADIUS + 1):
                offsets.append(-0.35 * float(x * x + y * y))
        self.spatial_logits = torch.nn.Parameter(torch.tensor(offsets, dtype=torch.float32))
        self.color_penalty = torch.nn.Parameter(torch.tensor(1.0))
        self.normal_penalty = torch.nn.Parameter(torch.tensor(2.0))
        self.albedo_penalty = torch.nn.Parameter(torch.tensor(2.0))
        self.depth_penalty = torch.nn.Parameter(torch.tensor(2.0))
        self.blend = torch.nn.Parameter(torch.tensor(1.5))

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        color = x[:, 0:3]
        normal = x[:, 3:6]
        albedo = x[:, 6:9]
        depth = x[:, 9:10]

        padded = F.pad(
            x,
            (MAX_FILTER_RADIUS, MAX_FILTER_RADIUS, MAX_FILTER_RADIUS, MAX_FILTER_RADIUS),
            mode="replicate",
        )
        patches = F.unfold(padded, kernel_size=MAX_FILTER_SIZE)
        b, _, hw = patches.shape
        patches = patches.view(b, 10, MAX_FILTER_TAPS, hw)

        center_color = color.reshape(b, 3, 1, hw)
        center_normal = normal.reshape(b, 3, 1, hw)
        center_albedo = albedo.reshape(b, 3, 1, hw)
        center_depth = depth.reshape(b, 1, 1, hw)

        patch_color = patches[:, 0:3]
        patch_normal = patches[:, 3:6]
        patch_albedo = patches[:, 6:9]
        patch_depth = patches[:, 9:10]

        logits = self.spatial_logits.view(1, MAX_FILTER_TAPS, 1)
        if "color" in self.guides:
            color_diff = ((patch_color - center_color) ** 2).sum(dim=1)
            logits = logits - F.softplus(self.color_penalty) * color_diff
        if "normal" in self.guides:
            normal_diff = ((patch_normal - center_normal) ** 2).sum(dim=1)
            logits = logits - F.softplus(self.normal_penalty) * normal_diff
        if "albedo" in self.guides:
            albedo_diff = ((patch_albedo - center_albedo) ** 2).sum(dim=1)
            logits = logits - F.softplus(self.albedo_penalty) * albedo_diff
        if "depth" in self.guides:
            depth_diff = ((patch_depth - center_depth) ** 2).sum(dim=1)
            logits = logits - F.softplus(self.depth_penalty) * depth_diff

        active = []
        for oy in range(-MAX_FILTER_RADIUS, MAX_FILTER_RADIUS + 1):
            for ox in range(-MAX_FILTER_RADIUS, MAX_FILTER_RADIUS + 1):
                active.append(abs(ox) <= self.radius and abs(oy) <= self.radius)
        active_mask = torch.tensor(active, device=x.device, dtype=torch.bool).view(1, MAX_FILTER_TAPS, 1)
        logits = logits.masked_fill(~active_mask, -1.0e9)
        weights = torch.softmax(logits, dim=1).unsqueeze(1)
        luma_weights = LUMA_WEIGHTS.to(device=x.device, dtype=x.dtype).view(1, 3, 1, 1)
        patch_luma = torch.sum(patch_color * luma_weights, dim=1, keepdim=True)
        filtered_luma = (patch_luma * weights).sum(dim=2).view(b, 1, color.shape[2], color.shape[3])
        center_luma = luminance(color)
        blend = MAX_LUMA_BLEND * torch.sigmoid(self.blend)
        out_luma = torch.lerp(center_luma, filtered_luma, blend)
        chroma = color / torch.clamp(center_luma, min=0.0001)
        return torch.clamp(chroma * out_luma, 0.0, 1.0)


class DenoiserDataset:
    def __init__(
        self,
        root: Path,
        crop_size: int,
        frames: list[int],
        importance_prob: float,
        importance_stride: int,
    ) -> None:
        self.root = root
        self.crop_size = crop_size
        self.active_frames = frames
        self.importance_prob = importance_prob
        self.importance_stride = importance_stride
        self.cache: dict[tuple[str, int], np.memmap] = {}
        self.importance_cdf: np.ndarray | None = None
        self.importance_tiles: list[tuple[int, int, int]] = []
        with (root / "dataset_meta.json").open("r", encoding="utf-8") as f:
            self.meta = json.load(f)
        self.width = int(self.meta["width"])
        self.height = int(self.meta["height"])
        self.frames = int(self.meta["frames"])
        if self.importance_prob > 0.0:
            self._build_importance_cdf()

    def _rgba16f(self, name: str, frame: int) -> np.ndarray:
        key = (name, frame)
        if key not in self.cache:
            path = self.root / f"{name}_{frame:06}.rgba16f"
            self.cache[key] = np.memmap(path, dtype=np.float16, mode="r").reshape(
                self.height, self.width, 4
            )
        return self.cache[key]

    def _build_importance_cdf(self) -> None:
        weights = []
        step = self.importance_stride
        half = self.crop_size // 2
        x_max = self.width - self.crop_size
        y_max = self.height - self.crop_size
        for frame in self.active_frames:
            noisy = self._rgba16f("input_color", frame)[::step, ::step, 0:3].astype(np.float32)
            target = self._rgba16f("target_color", frame)[::step, ::step, 0:3].astype(np.float32)
            error = np.mean(np.abs(target - noisy), axis=2)
            for ty in range(error.shape[0]):
                for tx in range(error.shape[1]):
                    cx = min(max(tx * step, half), self.width - half - 1)
                    cy = min(max(ty * step, half), self.height - half - 1)
                    x0 = min(max(cx - half, 0), x_max)
                    y0 = min(max(cy - half, 0), y_max)
                    self.importance_tiles.append((frame, x0, y0))
                    weights.append(float(error[ty, tx]) + 0.00001)

        cdf = np.cumsum(np.asarray(weights, dtype=np.float64))
        if cdf[-1] <= 0.0:
            self.importance_cdf = None
            self.importance_tiles.clear()
            return
        self.importance_cdf = cdf / cdf[-1]

    def _sample_location(self) -> tuple[int, int, int]:
        if self.importance_cdf is not None and random.random() < self.importance_prob:
            index = int(np.searchsorted(self.importance_cdf, random.random(), side="right"))
            index = min(index, len(self.importance_tiles) - 1)
            return self.importance_tiles[index]

        frame = random.choice(self.active_frames)
        x0 = random.randrange(0, self.width - self.crop_size + 1)
        y0 = random.randrange(0, self.height - self.crop_size + 1)
        return frame, x0, y0

    def sample(self) -> tuple[torch.Tensor, torch.Tensor]:
        frame, x0, y0 = self._sample_location()
        crop = np.s_[y0 : y0 + self.crop_size, x0 : x0 + self.crop_size]

        color = self._rgba16f("input_color", frame)[crop][..., 0:3].astype(np.float32)
        normal = self._rgba16f("input_normal", frame)[crop][..., 0:3].astype(np.float32)
        albedo = self._rgba16f("input_albedo", frame)[crop][..., 0:3].astype(np.float32)
        depth = self._rgba16f("input_depth", frame)[crop][..., 0:1].astype(np.float32)
        target = self._rgba16f("target_color", frame)[crop][..., 0:3].astype(np.float32)

        inp = np.concatenate([color, normal, albedo, depth], axis=2)
        inp_t = torch.from_numpy(inp).permute(2, 0, 1)
        target_t = torch.from_numpy(target).permute(2, 0, 1)
        return inp_t, target_t


def make_batch(dataset: DenoiserDataset, batch_size: int, device: str) -> tuple[torch.Tensor, torch.Tensor]:
    batch = [dataset.sample() for _ in range(batch_size)]
    inputs = torch.stack([item[0] for item in batch]).to(device)
    targets = torch.stack([item[1] for item in batch]).to(device)
    return inputs, targets


def loss_for(pred: torch.Tensor, inputs: torch.Tensor, targets: torch.Tensor, high_weight: float) -> torch.Tensor:
    pred_luma = luminance(pred)
    target_luma = luminance(targets)
    input_luma = luminance(inputs[:, 0:3])
    luma_error = pred_luma - target_luma
    per_pixel = torch.abs(luma_error) + 0.25 * luma_error * luma_error
    if high_weight <= 0.0:
        return torch.mean(per_pixel)

    with torch.no_grad():
        source_error = torch.abs(target_luma - input_luma)
        source_error = source_error / (torch.mean(source_error) + 0.000001)
        pixel_weight = 1.0 + high_weight * torch.clamp(source_error, 0.0, 6.0)
    return torch.sum(per_pixel * pixel_weight) / torch.sum(pixel_weight)


@torch.no_grad()
def validate(
    model: LearnedFilter,
    dataset: DenoiserDataset,
    batch_size: int,
    steps: int,
    device: str,
    high_weight: float,
) -> float:
    model.eval()
    losses = []
    for _ in range(steps):
        inputs, targets = make_batch(dataset, batch_size, device)
        losses.append(float(loss_for(model(inputs), inputs, targets, high_weight).detach().cpu()))
    return sum(losses) / len(losses)


def export_weights(model: LearnedFilter, output: Path) -> None:
    weights = np.concatenate(
        [
            np.array(
                [
                    float(model.radius),
                    float(model.blend.detach().cpu()),
                    float(model.color_penalty.detach().cpu()) if "color" in model.guides else DISABLED_PENALTY,
                    float(model.normal_penalty.detach().cpu()) if "normal" in model.guides else DISABLED_PENALTY,
                    float(model.albedo_penalty.detach().cpu()) if "albedo" in model.guides else DISABLED_PENALTY,
                    float(model.depth_penalty.detach().cpu()) if "depth" in model.guides else DISABLED_PENALTY,
                ],
                dtype="<f4",
            ),
            model.spatial_logits.detach().cpu().numpy().astype("<f4").reshape(-1),
        ]
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    weights.tofile(output)
    print(f"wrote {output} ({weights.size} f32 values)")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dataset", type=Path)
    parser.add_argument("--output", type=Path, default=Path("resources/denoiser_weights.bin"))
    parser.add_argument("--epochs", type=int, default=8)
    parser.add_argument("--steps-per-epoch", type=int, default=256)
    parser.add_argument("--batch-size", type=int, default=8)
    parser.add_argument("--crop-size", type=int, default=96)
    parser.add_argument("--lr", type=float, default=2e-4)
    parser.add_argument("--val-steps", type=int, default=32)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--importance-prob", type=float, default=0.7)
    parser.add_argument("--importance-stride", type=int, default=16)
    parser.add_argument("--high-weight", type=float, default=3.0)
    parser.add_argument("--radius", type=int, choices=[1, 2, 3, 4], default=1)
    parser.add_argument(
        "--guides",
        default="normal,albedo,depth",
        help="comma-separated guide terms: color,normal,albedo,depth or none",
    )
    parser.add_argument("--device", default="mps" if torch.backends.mps.is_available() else "cpu")
    args = parser.parse_args()

    guides = {part.strip() for part in args.guides.split(",") if part.strip() and part.strip() != "none"}
    valid_guides = {"color", "normal", "albedo", "depth"}
    unknown_guides = guides - valid_guides
    if unknown_guides:
        raise ValueError(f"unknown guide(s): {', '.join(sorted(unknown_guides))}")

    random.seed(args.seed)
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)

    with (args.dataset / "dataset_meta.json").open("r", encoding="utf-8") as f:
        meta = json.load(f)
    frame_count = int(meta["frames"])
    val_count = max(1, frame_count // 5)
    train_frames = list(range(0, max(1, frame_count - val_count)))
    val_frames = list(range(max(0, frame_count - val_count), frame_count))

    train_dataset = DenoiserDataset(
        args.dataset,
        args.crop_size,
        train_frames,
        args.importance_prob,
        args.importance_stride,
    )
    val_dataset = DenoiserDataset(
        args.dataset,
        args.crop_size,
        val_frames,
        args.importance_prob,
        args.importance_stride,
    )
    model = LearnedFilter(args.radius, guides).to(args.device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr)

    for epoch in range(args.epochs):
        losses = []
        model.train()
        for _ in range(args.steps_per_epoch):
            inputs, targets = make_batch(train_dataset, args.batch_size, args.device)

            pred = model(inputs)
            loss = loss_for(pred, inputs, targets, args.high_weight)
            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            optimizer.step()
            losses.append(float(loss.detach().cpu()))

        print(f"epoch {epoch + 1}/{args.epochs} loss {sum(losses) / len(losses):.6f}")

    val_loss = validate(model, val_dataset, args.batch_size, args.val_steps, args.device, args.high_weight)
    print(f"val_loss {val_loss:.6f}")
    export_weights(model, args.output)


if __name__ == "__main__":
    main()
