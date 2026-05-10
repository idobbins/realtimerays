#!/usr/bin/env python3
import argparse
import json
import random
from pathlib import Path

import numpy as np
import torch
from torch.nn import functional as F


MAX_FILTER_RADIUS = 2
MAX_FILTER_SIZE = MAX_FILTER_RADIUS * 2 + 1
MAX_FILTER_TAPS = MAX_FILTER_SIZE * MAX_FILTER_SIZE
DISABLED_PENALTY = -20.0


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
        filtered = (patch_color * weights).sum(dim=2).view_as(color)
        blend = torch.sigmoid(self.blend)
        return torch.clamp(torch.lerp(color, filtered, blend), 0.0, 1.0)


class DenoiserDataset:
    def __init__(self, root: Path, crop_size: int) -> None:
        self.root = root
        self.crop_size = crop_size
        with (root / "dataset_meta.json").open("r", encoding="utf-8") as f:
            self.meta = json.load(f)
        self.width = int(self.meta["width"])
        self.height = int(self.meta["height"])
        self.frames = int(self.meta["frames"])

    def _rgba16f(self, name: str, frame: int) -> np.ndarray:
        path = self.root / f"{name}_{frame:06}.rgba16f"
        data = np.fromfile(path, dtype=np.float16)
        return data.reshape(self.height, self.width, 4).astype(np.float32)

    def sample(self) -> tuple[torch.Tensor, torch.Tensor]:
        frame = random.randrange(self.frames)
        x0 = random.randrange(0, self.width - self.crop_size + 1)
        y0 = random.randrange(0, self.height - self.crop_size + 1)
        crop = np.s_[y0 : y0 + self.crop_size, x0 : x0 + self.crop_size]

        color = self._rgba16f("input_color", frame)[crop][..., 0:3]
        normal = self._rgba16f("input_normal", frame)[crop][..., 0:3]
        albedo = self._rgba16f("input_albedo", frame)[crop][..., 0:3]
        depth = self._rgba16f("input_depth", frame)[crop][..., 0:1]
        target = self._rgba16f("target_color", frame)[crop][..., 0:3]

        inp = np.concatenate([color, normal, albedo, depth], axis=2)
        inp_t = torch.from_numpy(inp).permute(2, 0, 1)
        target_t = torch.from_numpy(target).permute(2, 0, 1)
        return inp_t, target_t


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
    parser.add_argument("--radius", type=int, choices=[1, 2], default=1)
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

    dataset = DenoiserDataset(args.dataset, args.crop_size)
    model = LearnedFilter(args.radius, guides).to(args.device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr)

    for epoch in range(args.epochs):
        losses = []
        model.train()
        for _ in range(args.steps_per_epoch):
            batch = [dataset.sample() for _ in range(args.batch_size)]
            inputs = torch.stack([item[0] for item in batch]).to(args.device)
            targets = torch.stack([item[1] for item in batch]).to(args.device)

            pred = model(inputs)
            loss = F.l1_loss(pred, targets) + 0.25 * F.mse_loss(pred, targets)
            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            optimizer.step()
            losses.append(float(loss.detach().cpu()))

        print(f"epoch {epoch + 1}/{args.epochs} loss {sum(losses) / len(losses):.6f}")

    export_weights(model, args.output)


if __name__ == "__main__":
    main()
