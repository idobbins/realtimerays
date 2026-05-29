# RGB CNN Denoiser Repro

The RGB-only CNN consumes only `input_color` and `target_color`. It does not
require normal, albedo, or depth at training or runtime.

Capture an RGB-only dataset:

```bash
cargo run --release -- dataset renders/denoiser_dataset_rgb --frames 24 --rgb-only
```

Train the 8-hidden-channel RGB residual CNN:

```bash
.venv/bin/python tools/train_denoiser.py renders/denoiser_dataset_rgb \
  --model rgb-cnn \
  --epochs 8 \
  --steps-per-epoch 200 \
  --batch-size 8 \
  --crop-size 96 \
  --val-steps 40 \
  --seed 24 \
  --importance-prob 0.9 \
  --importance-stride 16 \
  --importance-error-weight 1.0 \
  --importance-gradient-weight 2.5 \
  --importance-guide-weight 1.25 \
  --lpips-weight 0.30 \
  --l1-weight 0.60 \
  --gradient-weight 0.08 \
  --output resources/denoiser_rgb_cnn_weights.bin
```

Run the renderer with the RGB-only CNN:

```bash
cargo run --release -- --rgb-cnn-denoise
```

Benchmark the isolated postpass:

```bash
cargo run --release -- bench-denoiser --frames 960 --warmup 20 --rgb-cnn-denoise
```

Evaluate against validation crops:

```bash
.venv/bin/python tools/evaluate_denoiser.py renders/denoiser_dataset_rgb \
  --weights rgbcnn=resources/denoiser_rgb_cnn_weights.bin \
  --crop-size 256 \
  --crops-per-frame 4 \
  --lpips-net squeeze
```
