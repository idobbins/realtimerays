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

For wide-kernel experiments, pass a positive odd kernel size. Path-traced noise
is a sampling artifact, so the network may need a much broader neighborhood than
the default 3x3 to see enough context to infer the clean signal:

```bash
.venv/bin/python tools/train_denoiser.py renders/denoiser_dataset_rgb \
  --model rgb-cnn \
  --rgb-cnn-kernel-size 33 \
  --epochs 8 \
  --steps-per-epoch 200 \
  --batch-size 4 \
  --crop-size 96 \
  --val-steps 40 \
  --output resources/denoiser_rgb_cnn_weights.bin
```

The renderer infers dense RGB CNN kernel size from the exported weight count, so
replacing `resources/denoiser_rgb_cnn_weights.bin` with a 9x9, 17x17, or 33x33
export is enough to run a dense variant. Very wide dense kernels are expensive
because each pixel samples `kernel_size * kernel_size` texels per hidden channel.

Sparse and factorized layouts keep a wider footprint without dense 17x17 cost:

```bash
.venv/bin/python tools/train_denoiser.py renders/denoiser_dataset_rgb \
  --model rgb-cnn \
  --rgb-cnn-layout axis17 \
  --epochs 8 \
  --steps-per-epoch 200 \
  --batch-size 4 \
  --crop-size 96 \
  --val-steps 40 \
  --output resources/denoiser_rgb_cnn_weights.bin
```

For luminance-only correction, train with `--rgb-cnn-mode luma`. This predicts
a scalar brightness correction and reapplies the source pixel's chroma, with a
small local-chroma fallback only for very dark center pixels:

```bash
.venv/bin/python tools/train_denoiser.py renders/denoiser_dataset_rgb \
  --model rgb-cnn \
  --rgb-cnn-mode luma \
  --rgb-cnn-layout dense \
  --rgb-cnn-kernel-size 9 \
  --epochs 8 \
  --steps-per-epoch 200 \
  --batch-size 4 \
  --crop-size 96 \
  --val-steps 40 \
  --output resources/denoiser_rgb_cnn_weights.bin
```

Run the renderer with the RGB-only CNN:

```bash
cargo run --release -- --rgb-cnn-denoise --rgb-cnn-layout dense --rgb-cnn-mode luma
```

For sparse reconstruction experiments, train with `--rgb-cnn-mode recon`. This
adds one input channel: `input_color.a`, interpreted as a coverage mask. Missing
pixels are zero RGB with alpha/coverage 0, and the model predicts direct RGB
rather than a residual from the center pixel. Existing dense datasets can be
masked synthetically during training:

```bash
.venv/bin/python tools/train_denoiser.py renders/denoiser_dataset_cnn64 \
  --model rgb-cnn \
  --rgb-cnn-mode recon \
  --rgb-cnn-layout dense \
  --rgb-cnn-kernel-size 17 \
  --sparse-coverage 0.5 \
  --epochs 8 \
  --steps-per-epoch 200 \
  --batch-size 4 \
  --crop-size 96 \
  --val-steps 40 \
  --output resources/denoiser_recon_cnn_weights.bin
```

You can also capture sparse input directly. `--coverage 0.5 --input-spp 1`
means roughly half a ray per pixel on average:

```bash
cargo run --release -- dataset renders/denoiser_dataset_sparse \
  --frames 24 \
  --rgb-only \
  --input-spp 1 \
  --coverage 0.5
```

Run the reconstruction mode with the same sparse coverage:

```bash
cargo run --release -- --rgb-cnn-denoise --rgb-cnn-mode recon --spp 1 --coverage 0.5
```

Benchmark the isolated postpass:

```bash
cargo run --release -- bench-denoiser --frames 960 --warmup 20 --rgb-cnn-denoise --rgb-cnn-layout dense --rgb-cnn-mode luma
```

Evaluate against validation crops:

```bash
.venv/bin/python tools/evaluate_denoiser.py renders/denoiser_dataset_rgb \
  --weights rgbcnn=resources/denoiser_rgb_cnn_weights.bin \
  --rgb-cnn-layout dense \
  --rgb-cnn-mode luma \
  --crop-size 256 \
  --crops-per-frame 4 \
  --lpips-net squeeze
```

Matrix runs can sweep RGB CNN dense kernel sizes and layouts:

```bash
.venv/bin/python tools/run_denoiser_matrix.py \
  --dataset renders/denoiser_dataset_rgb \
  --rgb-cnn-kernels 3,9,17 \
  --rgb-cnn-layouts dense,sparse-wide,dilated3,dilated5,axis17
```
