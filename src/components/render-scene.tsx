"use client";

import { useEffect, useRef, useState } from "react";

import { blitWGSL, pathTracerWGSL } from "@/lib/pathtracer.wgsl";
import type {
  CameraType,
  RenderSettings,
  RenderSphere,
  SphereMaterial,
} from "@/lib/render-settings";

type Stats = { fps: number; samples: number; supported: boolean; error?: string };
const uniformBufferSize = 112;

const materialTypeIds: Record<SphereMaterial, number> = {
  diffuse: 0,
  metal: 1,
  light: 2,
  glass: 3,
};

function packSpheres(spheres: RenderSphere[]) {
  const buffer = new Float32Array(spheres.length * 12);

  spheres.forEach((sphere, index) => {
    const offset = index * 12;
    buffer[offset] = sphere.center[0];
    buffer[offset + 1] = sphere.center[1];
    buffer[offset + 2] = sphere.center[2];
    buffer[offset + 3] = sphere.radius;
    buffer[offset + 4] = sphere.albedo[0];
    buffer[offset + 5] = sphere.albedo[1];
    buffer[offset + 6] = sphere.albedo[2];
    buffer[offset + 7] = materialTypeIds[sphere.material];
    buffer[offset + 8] = sphere.emission[0];
    buffer[offset + 9] = sphere.emission[1];
    buffer[offset + 10] = sphere.emission[2];
    buffer[offset + 11] = sphere.fuzz;
  });

  return buffer;
}

const cameraTypeIds: Record<CameraType, number> = {
  perspective: 0,
  orthographic: 1,
  "thin-lens": 2,
};

export function RenderScene({ settings }: { settings: RenderSettings }) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [stats, setStats] = useState<Stats>({ fps: 0, samples: 0, supported: true });
  const cameraRef = useRef({ yaw: 0.6, pitch: 0.15, dist: 7.5 });
  const settingsRef = useRef(settings);
  const resizeRef = useRef<(() => void) | null>(null);
  const sampleIndexRef = useRef(0);
  const dirtyRef = useRef(true);

  useEffect(() => {
    settingsRef.current = settings;
    dirtyRef.current = true;
    resizeRef.current?.();
  }, [settings]);

  useEffect(() => {
    let cancelled = false;
    let raf = 0;
    const cleanupFns: Array<() => void> = [];

    const setup = async () => {
      const canvas = canvasRef.current;

      if (!canvas) {
        return;
      }

      if (!("gpu" in navigator)) {
        setStats({
          fps: 0,
          samples: 0,
          supported: false,
          error: "WebGPU is not available in this browser.",
        });
        return;
      }

      const gpu = (navigator as Navigator & { gpu: GPU }).gpu;
      const adapter = await gpu.requestAdapter();

      if (!adapter) {
        setStats({ fps: 0, samples: 0, supported: false, error: "No GPU adapter found." });
        return;
      }

      const device = await adapter.requestDevice();

      if (cancelled) {
        device.destroy();
        return;
      }

      const context = canvas.getContext("webgpu");

      if (!context) {
        setStats({ fps: 0, samples: 0, supported: false, error: "WebGPU context unavailable." });
        device.destroy();
        return;
      }

      const format = gpu.getPreferredCanvasFormat();
      context.configure({ device, format, alphaMode: "premultiplied" });

      let width = 1;
      let height = 1;
      let accumA: GPUTexture | undefined;
      let accumB: GPUTexture | undefined;
      let outTex: GPUTexture | undefined;
      let bindGroupAB: GPUBindGroup;
      let bindGroupBA: GPUBindGroup;
      let blitBindGroup: GPUBindGroup;
      let pingPong = 0;

      const uniformBuffer = device.createBuffer({
        size: uniformBufferSize,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
      });

      const createSphereBuffer = (sphereData: Float32Array) =>
        device.createBuffer({
          size: sphereData.byteLength,
          usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
        });

      let sphereData = packSpheres(settingsRef.current.sceneSpheres);
      let sphereBufferByteLength = sphereData.byteLength;
      let sphereBuffer = createSphereBuffer(sphereData);
      device.queue.writeBuffer(sphereBuffer, 0, sphereData);

      const computePipeline = device.createComputePipeline({
        layout: "auto",
        compute: {
          module: device.createShaderModule({ code: pathTracerWGSL }),
          entryPoint: "main",
        },
      });
      const blitPipeline = device.createRenderPipeline({
        layout: "auto",
        vertex: { module: device.createShaderModule({ code: blitWGSL }), entryPoint: "vs" },
        fragment: {
          module: device.createShaderModule({ code: blitWGSL }),
          entryPoint: "fs",
          targets: [{ format }],
        },
        primitive: { topology: "triangle-list" },
      });
      const sampler = device.createSampler({ magFilter: "linear", minFilter: "linear" });

      const allocateTextures = (nextWidth: number, nextHeight: number) => {
        accumA?.destroy();
        accumB?.destroy();
        outTex?.destroy();

        accumA = device.createTexture({
          size: [nextWidth, nextHeight],
          format: "rgba32float",
          usage: GPUTextureUsage.STORAGE_BINDING,
        });
        accumB = device.createTexture({
          size: [nextWidth, nextHeight],
          format: "rgba32float",
          usage: GPUTextureUsage.STORAGE_BINDING,
        });
        outTex = device.createTexture({
          size: [nextWidth, nextHeight],
          format: "rgba8unorm",
          usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING,
        });

        const outView = outTex.createView();
        const computeLayout = computePipeline.getBindGroupLayout(0);
        bindGroupAB = device.createBindGroup({
          layout: computeLayout,
          entries: [
            { binding: 0, resource: { buffer: uniformBuffer } },
            { binding: 1, resource: { buffer: sphereBuffer } },
            { binding: 2, resource: accumA.createView() },
            { binding: 3, resource: accumB.createView() },
            { binding: 4, resource: outView },
          ],
        });
        bindGroupBA = device.createBindGroup({
          layout: computeLayout,
          entries: [
            { binding: 0, resource: { buffer: uniformBuffer } },
            { binding: 1, resource: { buffer: sphereBuffer } },
            { binding: 2, resource: accumB.createView() },
            { binding: 3, resource: accumA.createView() },
            { binding: 4, resource: outView },
          ],
        });
        blitBindGroup = device.createBindGroup({
          layout: blitPipeline.getBindGroupLayout(0),
          entries: [
            { binding: 0, resource: outView },
            { binding: 1, resource: sampler },
          ],
        });
        pingPong = 0;
      };

      const resize = () => {
        const cssWidth = canvas.clientWidth;
        const cssHeight = canvas.clientHeight;
        let nextWidth = Math.max(2, Math.floor(cssWidth));
        let nextHeight = Math.max(2, Math.floor(cssHeight));
        const maxPixels = settingsRef.current.maxPixels;
        const pixels = nextWidth * nextHeight;

        if (pixels > maxPixels) {
          const scale = Math.sqrt(maxPixels / pixels);
          nextWidth = Math.floor(nextWidth * scale);
          nextHeight = Math.floor(nextHeight * scale);
        }

        if (nextWidth === width && nextHeight === height) {
          return;
        }

        width = nextWidth;
        height = nextHeight;
        canvas.width = nextWidth;
        canvas.height = nextHeight;
        allocateTextures(nextWidth, nextHeight);
        dirtyRef.current = true;
      };

      resizeRef.current = resize;
      const resizeObserver = new ResizeObserver(resize);
      resizeObserver.observe(canvas);
      resize();
      cleanupFns.push(() => resizeObserver.disconnect());

      let dragging = false;
      let lastX = 0;
      let lastY = 0;

      const onPointerDown = (event: PointerEvent) => {
        dragging = true;
        lastX = event.clientX;
        lastY = event.clientY;
        canvas.setPointerCapture(event.pointerId);
      };
      const onPointerMove = (event: PointerEvent) => {
        if (!dragging) {
          return;
        }

        const dx = event.clientX - lastX;
        const dy = event.clientY - lastY;
        lastX = event.clientX;
        lastY = event.clientY;
        cameraRef.current.yaw -= dx * 0.005;
        cameraRef.current.pitch = Math.max(
          -1.3,
          Math.min(1.3, cameraRef.current.pitch + dy * 0.005),
        );
        dirtyRef.current = true;
      };
      const onPointerUp = (event: PointerEvent) => {
        dragging = false;
        if (canvas.hasPointerCapture(event.pointerId)) {
          canvas.releasePointerCapture(event.pointerId);
        }
      };
      const onWheel = (event: WheelEvent) => {
        event.preventDefault();
        cameraRef.current.dist = Math.max(
          2.5,
          Math.min(20, cameraRef.current.dist * (1 + event.deltaY * 0.001)),
        );
        dirtyRef.current = true;
      };

      canvas.addEventListener("pointerdown", onPointerDown);
      canvas.addEventListener("pointermove", onPointerMove);
      canvas.addEventListener("pointerup", onPointerUp);
      canvas.addEventListener("pointercancel", onPointerUp);
      canvas.addEventListener("wheel", onWheel, { passive: false });
      cleanupFns.push(() => {
        canvas.removeEventListener("pointerdown", onPointerDown);
        canvas.removeEventListener("pointermove", onPointerMove);
        canvas.removeEventListener("pointerup", onPointerUp);
        canvas.removeEventListener("pointercancel", onPointerUp);
        canvas.removeEventListener("wheel", onWheel);
      });

      const uniformData = new ArrayBuffer(uniformBufferSize);
      const uniformFloats = new Float32Array(uniformData);
      const uniformInts = new Uint32Array(uniformData);
      let frame = 0;
      let fpsFrames = 0;
      let fpsTimer = performance.now();

      const render = () => {
        if (cancelled) {
          return;
        }

        if (dirtyRef.current) {
          sampleIndexRef.current = 0;
          dirtyRef.current = false;
        }

        const { yaw, pitch, dist } = cameraRef.current;
        const cy = Math.cos(yaw);
        const sy = Math.sin(yaw);
        const cp = Math.cos(pitch);
        const sp = Math.sin(pitch);
        const target: [number, number, number] = [0, 0.2, 0];
        const cameraPosition: [number, number, number] = [
          target[0] + dist * cp * sy,
          target[1] + dist * sp,
          target[2] + dist * cp * cy,
        ];
        const forward: [number, number, number] = [
          target[0] - cameraPosition[0],
          target[1] - cameraPosition[1],
          target[2] - cameraPosition[2],
        ];
        const forwardLength = Math.hypot(forward[0], forward[1], forward[2]) || 1;
        forward[0] /= forwardLength;
        forward[1] /= forwardLength;
        forward[2] /= forwardLength;

        const worldUp: [number, number, number] = [0, 1, 0];
        const right: [number, number, number] = [
          forward[1] * worldUp[2] - forward[2] * worldUp[1],
          forward[2] * worldUp[0] - forward[0] * worldUp[2],
          forward[0] * worldUp[1] - forward[1] * worldUp[0],
        ];
        const rightLength = Math.hypot(right[0], right[1], right[2]) || 1;
        right[0] /= rightLength;
        right[1] /= rightLength;
        right[2] /= rightLength;
        const up: [number, number, number] = [
          right[1] * forward[2] - right[2] * forward[1],
          right[2] * forward[0] - right[0] * forward[2],
          right[0] * forward[1] - right[1] * forward[0],
        ];

        uniformFloats[0] = width;
        uniformFloats[1] = height;
        const currentSettings = settingsRef.current;
        const renderFrame = currentSettings.temporalAccumulation ? frame : 0;
        const accumulationSampleIndex = currentSettings.temporalAccumulation
          ? sampleIndexRef.current
          : 0;

        uniformInts[2] = renderFrame >>> 0;
        uniformInts[3] = accumulationSampleIndex >>> 0;
        uniformFloats[4] = cameraPosition[0];
        uniformFloats[5] = cameraPosition[1];
        uniformFloats[6] = cameraPosition[2];
        uniformFloats[7] = 0;
        uniformFloats[8] = forward[0];
        uniformFloats[9] = forward[1];
        uniformFloats[10] = forward[2];
        uniformFloats[11] = 0;
        uniformFloats[12] = right[0];
        uniformFloats[13] = right[1];
        uniformFloats[14] = right[2];
        uniformFloats[15] = 0;
        uniformFloats[16] = up[0];
        uniformFloats[17] = up[1];
        uniformFloats[18] = up[2];
        uniformFloats[19] = (currentSettings.fovDegrees * Math.PI) / 180;
        uniformInts[20] = cameraTypeIds[currentSettings.cameraType] ?? 0;
        uniformFloats[21] = currentSettings.orthoScale;
        uniformFloats[22] = currentSettings.apertureRadius;
        uniformFloats[23] = currentSettings.focusDistance;
        uniformInts[24] = currentSettings.samplesPerDispatch >>> 0;
        uniformInts[25] = currentSettings.maxBounces >>> 0;
        device.queue.writeBuffer(uniformBuffer, 0, uniformData);
        sphereData = packSpheres(currentSettings.sceneSpheres);

        if (sphereData.byteLength !== sphereBufferByteLength) {
          sphereBuffer.destroy();
          sphereBuffer = createSphereBuffer(sphereData);
          sphereBufferByteLength = sphereData.byteLength;
          allocateTextures(width, height);
        }

        device.queue.writeBuffer(sphereBuffer, 0, sphereData);

        const encoder = device.createCommandEncoder();
        const computePass = encoder.beginComputePass();
        computePass.setPipeline(computePipeline);
        computePass.setBindGroup(0, pingPong === 0 ? bindGroupAB : bindGroupBA);
        computePass.dispatchWorkgroups(Math.ceil(width / 8), Math.ceil(height / 8));
        computePass.end();
        pingPong = 1 - pingPong;

        const renderPass = encoder.beginRenderPass({
          colorAttachments: [
            {
              view: context.getCurrentTexture().createView(),
              clearValue: { r: 0, g: 0, b: 0, a: 1 },
              loadOp: "clear",
              storeOp: "store",
            },
          ],
        });
        renderPass.setPipeline(blitPipeline);
        renderPass.setBindGroup(0, blitBindGroup);
        renderPass.draw(3);
        renderPass.end();
        device.queue.submit([encoder.finish()]);

        sampleIndexRef.current += 1;
        frame += 1;
        fpsFrames += 1;

        const now = performance.now();
        if (now - fpsTimer > 500) {
          setStats({
            fps: (fpsFrames * 1000) / (now - fpsTimer),
            samples: currentSettings.temporalAccumulation
              ? sampleIndexRef.current * currentSettings.samplesPerDispatch
              : currentSettings.samplesPerDispatch,
            supported: true,
          });
          fpsTimer = now;
          fpsFrames = 0;
        }

        raf = requestAnimationFrame(render);
      };

      raf = requestAnimationFrame(render);
      cleanupFns.push(() => cancelAnimationFrame(raf));
      cleanupFns.push(() => {
        resizeRef.current = null;
        accumA?.destroy();
        accumB?.destroy();
        outTex?.destroy();
        sphereBuffer.destroy();
        device.destroy();
      });
    };

    setup().catch((error: unknown) => {
      setStats({
        fps: 0,
        samples: 0,
        supported: false,
        error: error instanceof Error ? error.message : String(error),
      });
    });

    return () => {
      cancelled = true;
      cancelAnimationFrame(raf);
      cleanupFns.forEach((cleanup) => cleanup());
    };
  }, []);

  const fpsTone =
    stats.fps >= 50
      ? "text-emerald-400"
      : stats.fps >= 30
        ? "text-yellow-300"
        : stats.fps >= 15
          ? "text-orange-400"
          : "text-red-400";

  return (
    <div
      className="relative size-full overflow-hidden"
      aria-label="WebGPU path tracing render preview"
    >
      <canvas
        ref={canvasRef}
        className="block size-full cursor-grab touch-none active:cursor-grabbing"
      />
      {!stats.supported ? (
        <div className="absolute inset-0 flex items-center justify-center bg-background/85 p-6 backdrop-blur-sm">
          <div className="max-w-md rounded-lg border border-border bg-card p-6 text-center text-card-foreground shadow-lg">
            <h2 className="mb-2 text-lg font-semibold">WebGPU not available</h2>
            <p className="text-sm text-muted-foreground">
              {stats.error ?? "This browser/device does not expose a WebGPU adapter."} Try the
              latest Chrome or Edge on a desktop with a GPU.
            </p>
          </div>
        </div>
      ) : (
        <div className="pointer-events-none absolute top-3 left-3 font-mono text-[11px] leading-4 font-medium tracking-wide text-white drop-shadow-[0_1px_2px_rgba(0,0,0,0.9)] uppercase tabular-nums">
          <div>
            FPS <span className={fpsTone}>{stats.fps.toFixed(1)}</span>
          </div>
          <div className="text-white/85">Samples {stats.samples}</div>
        </div>
      )}
      <div className="pointer-events-none absolute right-4 bottom-4 rounded-md border border-border/60 bg-card/70 px-3 py-2 text-xs text-muted-foreground backdrop-blur-md">
        Drag to orbit · Scroll to zoom
      </div>
    </div>
  );
}
