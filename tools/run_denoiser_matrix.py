#!/usr/bin/env python3
import argparse
import csv
import re
import shutil
import subprocess
import sys
from pathlib import Path


VARIANTS = [
    {
        "name": "sparse13_spatial",
        "model": "filter",
        "tap_pattern": "sparse13",
        "guides": "none",
        "resource": "resources/denoiser_weights.bin",
        "bench_flag": "--filter-denoise",
    },
    {
        "name": "sparse13_nd",
        "model": "filter",
        "tap_pattern": "sparse13",
        "guides": "normal,depth",
        "resource": "resources/denoiser_weights.bin",
        "bench_flag": "--filter-denoise",
    },
    {
        "name": "sparse13_nad",
        "model": "filter",
        "tap_pattern": "sparse13",
        "guides": "normal,albedo,depth",
        "resource": "resources/denoiser_weights.bin",
        "bench_flag": "--filter-denoise",
    },
    {
        "name": "rgbcnn_h8",
        "model": "rgb-cnn",
        "tap_pattern": None,
        "guides": "none",
        "resource": "resources/denoiser_rgb_cnn_weights.bin",
        "bench_flag": "--rgb-cnn-denoise",
    },
]


def run(cmd: list[str], cwd: Path) -> str:
    print("$ " + " ".join(cmd), flush=True)
    proc = subprocess.run(
        cmd,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    print(proc.stdout, end="", flush=True)
    proc.check_returncode()
    return proc.stdout


def parse_loss(output: str) -> float:
    matches = re.findall(r"epoch \d+/\d+ loss ([0-9.]+)", output)
    return float(matches[-1]) if matches else float("nan")


def parse_val_loss(output: str) -> float:
    matches = re.findall(r"val_loss ([0-9.]+)", output)
    return float(matches[-1]) if matches else float("nan")


def parse_bench(output: str) -> tuple[float, float]:
    match = re.search(r"avg_ms=([0-9.]+)(?: fps=([0-9.]+))?", output)
    if not match:
        return float("nan"), float("nan")
    avg_ms = float(match.group(1))
    fps = float(match.group(2)) if match.group(2) is not None else 1000.0 / avg_ms
    return avg_ms, fps


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", type=Path, default=Path("/tmp/realtimerays_dataset_uv_small"))
    parser.add_argument("--out-dir", type=Path, default=Path("renders/denoiser_matrix"))
    parser.add_argument("--epochs", type=int, default=1)
    parser.add_argument("--steps-per-epoch", type=int, default=8)
    parser.add_argument("--batch-size", type=int, default=2)
    parser.add_argument("--crop-size", type=int, default=48)
    parser.add_argument("--bench-frames", type=int, default=60)
    parser.add_argument("--bench-warmup", type=int, default=10)
    parser.add_argument("--val-steps", type=int, default=32)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--importance-prob", type=float, default=0.7)
    parser.add_argument("--importance-stride", type=int, default=16)
    parser.add_argument("--high-weight", type=float, default=3.0)
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    args.out_dir.mkdir(parents=True, exist_ok=True)
    rows = []

    for variant in VARIANTS:
        name = variant["name"]
        weights_path = args.out_dir / f"{name}.bin"
        train_cmd = [
            sys.executable,
            "tools/train_denoiser.py",
            str(args.dataset),
            "--model",
            variant["model"],
            "--epochs",
            str(args.epochs),
            "--steps-per-epoch",
            str(args.steps_per_epoch),
            "--batch-size",
            str(args.batch_size),
            "--crop-size",
            str(args.crop_size),
            "--val-steps",
            str(args.val_steps),
            "--seed",
            str(args.seed),
            "--importance-prob",
            str(args.importance_prob),
            "--importance-stride",
            str(args.importance_stride),
            "--high-weight",
            str(args.high_weight),
            "--guides",
            variant["guides"],
            "--output",
            str(weights_path),
        ]
        if variant["tap_pattern"] is not None:
            train_cmd.extend(["--tap-pattern", variant["tap_pattern"]])
        train_output = run(train_cmd, repo)

        resource_path = repo / variant["resource"]
        shutil.copyfile(weights_path, resource_path)
        bench_output = run(
            [
                "cargo",
                "run",
                "--release",
                "--",
                "bench-denoiser",
                "--frames",
                str(args.bench_frames),
                "--warmup",
                str(args.bench_warmup),
                variant["bench_flag"],
            ],
            repo,
        )
        avg_ms, fps = parse_bench(bench_output)
        rows.append(
            {
                "name": name,
                "model": variant["model"],
                "tap_pattern": variant["tap_pattern"] or "",
                "guides": variant["guides"],
                "loss": parse_loss(train_output),
                "val_loss": parse_val_loss(train_output),
                "avg_ms": avg_ms,
                "fps": fps,
                "weights": str(weights_path),
                "resource": variant["resource"],
            }
        )

    csv_path = args.out_dir / "results.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=[
                "name",
                "model",
                "tap_pattern",
                "guides",
                "loss",
                "val_loss",
                "avg_ms",
                "fps",
                "weights",
                "resource",
            ],
        )
        writer.writeheader()
        writer.writerows(rows)

    best = min(rows, key=lambda row: (row["val_loss"], row["avg_ms"]))
    shutil.copyfile(best["weights"], repo / best["resource"])
    print(f"wrote {csv_path}")
    print(f"selected {best['name']} -> {best['resource']}")


if __name__ == "__main__":
    main()
