# Sparse13 Denoiser Repro

Dataset used for the sparse-kernel pass:

```bash
cargo run --release -- dataset renders/denoiser_dataset_cnn64 --frames 24
```

The dataset generator currently renders 2 spp inputs and 64 spp targets.

Train the selected 13-tap sparse 9x9 normal+depth filter:

```bash
.venv/bin/python tools/train_denoiser.py renders/denoiser_dataset_cnn64 \
  --model filter \
  --tap-pattern sparse13 \
  --epochs 8 \
  --steps-per-epoch 200 \
  --batch-size 8 \
  --crop-size 96 \
  --val-steps 40 \
  --seed 24 \
  --guides normal,depth \
  --importance-prob 0.9 \
  --importance-stride 16 \
  --importance-error-weight 1.0 \
  --importance-gradient-weight 2.5 \
  --importance-guide-weight 1.25 \
  --lpips-weight 0.30 \
  --l1-weight 0.60 \
  --gradient-weight 0.08 \
  --output resources/denoiser_weights.bin
```

For a same-schedule 3x3 baseline comparison:

```bash
.venv/bin/python tools/train_denoiser.py renders/denoiser_dataset_cnn64 \
  --model filter \
  --tap-pattern dense3 \
  --epochs 8 \
  --steps-per-epoch 200 \
  --batch-size 8 \
  --crop-size 96 \
  --val-steps 40 \
  --seed 24 \
  --guides normal,depth \
  --importance-prob 0.9 \
  --importance-stride 16 \
  --importance-error-weight 1.0 \
  --importance-gradient-weight 2.5 \
  --importance-guide-weight 1.25 \
  --lpips-weight 0.30 \
  --l1-weight 0.60 \
  --gradient-weight 0.08 \
  --output /tmp/realtimerays_dense3_nd_64_long.bin
```

Evaluate LPIPS/L1/RMSE against the 64 spp validation frames:

```bash
.venv/bin/python tools/evaluate_denoiser.py renders/denoiser_dataset_cnn64 \
  --weights dense3_64=/tmp/realtimerays_dense3_nd_64_long.bin \
  --weights selected=resources/denoiser_weights.bin \
  --crop-size 256 \
  --crops-per-frame 4 \
  --lpips-net squeeze
```

Latest selected result:

```text
noisy,0.166383,0.024712,0.058900,16
dense3_64,0.139361,0.018247,0.046424,16
selected,0.139343,0.018244,0.046418,16
```

Benchmark isolated denoiser throughput:

```bash
cargo run --release -- bench-denoiser --frames 960 --warmup 20 --filter-denoise
```

Write a noisy/current/reference crop comparison:

```bash
.venv/bin/python tools/compare_denoiser.py renders/denoiser_dataset_cnn64 \
  --weights dense3_64=/tmp/realtimerays_dense3_nd_64_long.bin \
  --weights selected=resources/denoiser_weights.bin \
  --output renders/denoiser_sparse13_comparison \
  --frame 23 \
  --crop-size 256
```
