"use client";

import { useEffect, useRef, useState } from "react";

import { blitWGSL, pathTracerWGSL } from "@/lib/pathtracer.wgsl";
import type {
  CameraType,
  RenderSettings,
  RenderSphere,
  RenderViewport,
  SphereMaterial,
  ToneMap,
} from "@/lib/render-settings";
import { cn } from "@/lib/utils";

type PaneRect = {
  x: number;
  y: number;
  width: number;
  height: number;
};

type PaneStats = PaneRect & {
  id: string;
  label: string;
  samples: number;
  toneMap: ToneMap;
};

type Stats = {
  fps: number;
  supported: boolean;
  width: number;
  height: number;
  panes: PaneStats[];
  error?: string;
};

type CameraBasis = {
  position: [number, number, number];
  forward: [number, number, number];
  right: [number, number, number];
  up: [number, number, number];
};

type PaneGpuState = {
  id: string;
  label: string;
  renderRect: PaneRect;
  displayRect: PaneRect;
  splitPresentation: boolean;
  width: number;
  height: number;
  uniformBuffer: GPUBuffer;
  displayUniformBuffer: GPUBuffer;
  accumA?: GPUTexture;
  accumB?: GPUTexture;
  bindGroupAB?: GPUBindGroup;
  bindGroupBA?: GPUBindGroup;
  blitBindGroupA?: GPUBindGroup;
  blitBindGroupB?: GPUBindGroup;
  pingPong: 0 | 1;
  sampleIndex: number;
  frame: number;
};

const uniformBufferSize = 112;
const displayUniformBufferSize = 16;

const materialTypeIds: Record<SphereMaterial, number> = {
  diffuse: 0,
  metal: 1,
  light: 2,
  glass: 3,
};

const cameraTypeIds: Record<CameraType, number> = {
  perspective: 0,
  orthographic: 1,
  "thin-lens": 2,
};

const toneMapIds: Record<ToneMap, number> = {
  reinhard: 0,
  aces: 1,
  linear: 2,
  none: 3,
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

function accumulationSignature(settings: RenderSettings) {
  return JSON.stringify({
    cameraType: settings.cameraType,
    fovDegrees: settings.fovDegrees,
    orthoScale: settings.orthoScale,
    apertureRadius: settings.apertureRadius,
    focusDistance: settings.focusDistance,
    sceneSpheres: settings.sceneSpheres,
    samplesPerDispatch: settings.samplesPerDispatch,
    maxBounces: settings.maxBounces,
    temporalAccumulation: settings.temporalAccumulation,
  });
}

function layoutPanes(viewports: RenderViewport[], width: number, height: number) {
  if (viewports.length <= 1) {
    return [{ renderRect: { x: 0, y: 0, width, height }, displayRect: { x: 0, y: 0, width, height } }];
  }

  const splitIndex = viewports.findIndex((viewport) => viewport.presentation === "split-left");
  const splitMateIndex = viewports.findIndex((viewport) => viewport.presentation === "split-right");

  if (splitIndex >= 0 && splitMateIndex >= 0) {
    const halfWidth = Math.floor(width / 2);

    return viewports.map((viewport) => {
      const fullRect = { x: 0, y: 0, width, height };
      const displayRect =
        viewport.presentation === "split-left"
          ? { x: 0, y: 0, width: halfWidth, height }
          : viewport.presentation === "split-right"
            ? { x: halfWidth, y: 0, width: width - halfWidth, height }
            : fullRect;

      return { renderRect: fullRect, displayRect };
    });
  }

  const columns = viewports.length === 2 ? 2 : Math.ceil(Math.sqrt(viewports.length));
  const rows = Math.ceil(viewports.length / columns);

  return viewports.map((_, index) => {
    const column = index % columns;
    const row = Math.floor(index / columns);
    const x = Math.floor((column * width) / columns);
    const y = Math.floor((row * height) / rows);
    const nextX = Math.floor(((column + 1) * width) / columns);
    const nextY = Math.floor(((row + 1) * height) / rows);

    const rect = {
      x,
      y,
      width: Math.max(1, nextX - x),
      height: Math.max(1, nextY - y),
    };

    return { renderRect: rect, displayRect: rect };
  });
}

function buildCameraBasis({ yaw, pitch, dist }: { yaw: number; pitch: number; dist: number }) {
  const cy = Math.cos(yaw);
  const sy = Math.sin(yaw);
  const cp = Math.cos(pitch);
  const sp = Math.sin(pitch);
  const target: [number, number, number] = [0, 0.2, 0];
  const position: [number, number, number] = [
    target[0] + dist * cp * sy,
    target[1] + dist * sp,
    target[2] + dist * cp * cy,
  ];
  const forward: [number, number, number] = [
    target[0] - position[0],
    target[1] - position[1],
    target[2] - position[2],
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

  return { position, forward, right, up };
}

function destroyPaneTextures(pane: PaneGpuState) {
  pane.accumA?.destroy();
  pane.accumB?.destroy();
  pane.accumA = undefined;
  pane.accumB = undefined;
  pane.bindGroupAB = undefined;
  pane.bindGroupBA = undefined;
  pane.blitBindGroupA = undefined;
  pane.blitBindGroupB = undefined;
}

function destroyPane(pane: PaneGpuState) {
  destroyPaneTextures(pane);
  pane.uniformBuffer.destroy();
  pane.displayUniformBuffer.destroy();
}

export function RenderScene({ viewports }: { viewports: RenderViewport[] }) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [stats, setStats] = useState<Stats>({
    fps: 0,
    supported: true,
    width: 0,
    height: 0,
    panes: [],
  });
  const cameraRef = useRef({ yaw: 0.6, pitch: 0.15, dist: 7.5 });
  const viewportsRef = useRef(viewports);
  const viewportSignatureRef = useRef(new Map<string, string>());
  const dirtyViewportIdsRef = useRef(new Set(viewports.map((viewport) => viewport.id)));
  const resizeRef = useRef<(() => void) | null>(null);

  useEffect(() => {
    viewportsRef.current = viewports;

    const nextIds = new Set(viewports.map((viewport) => viewport.id));
    viewportSignatureRef.current.forEach((_, id) => {
      if (!nextIds.has(id)) {
        viewportSignatureRef.current.delete(id);
      }
    });

    viewports.forEach((viewport) => {
      const signature = accumulationSignature(viewport.settings);
      if (viewportSignatureRef.current.get(viewport.id) !== signature) {
        dirtyViewportIdsRef.current.add(viewport.id);
        viewportSignatureRef.current.set(viewport.id, signature);
      }
    });

    resizeRef.current?.();
  }, [viewports]);

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
          supported: false,
          width: 0,
          height: 0,
          panes: [],
          error: "WebGPU is not available in this browser.",
        });
        return;
      }

      const gpu = (navigator as Navigator & { gpu: GPU }).gpu;
      const adapter = await gpu.requestAdapter();

      if (!adapter) {
        setStats({
          fps: 0,
          supported: false,
          width: 0,
          height: 0,
          panes: [],
          error: "No GPU adapter found.",
        });
        return;
      }

      const device = await adapter.requestDevice();

      if (cancelled) {
        device.destroy();
        return;
      }

      const context = canvas.getContext("webgpu");

      if (!context) {
        setStats({
          fps: 0,
          supported: false,
          width: 0,
          height: 0,
          panes: [],
          error: "WebGPU context unavailable.",
        });
        device.destroy();
        return;
      }

      const format = gpu.getPreferredCanvasFormat();
      context.configure({ device, format, alphaMode: "premultiplied" });

      let width = 1;
      let height = 1;
      let sphereData = packSpheres(viewportsRef.current[0]?.settings.sceneSpheres ?? []);
      let sphereBufferByteLength = sphereData.byteLength;
      let sphereBuffer = device.createBuffer({
        size: Math.max(4, sphereData.byteLength),
        usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
      });
      if (sphereData.byteLength > 0) {
        device.queue.writeBuffer(sphereBuffer, 0, sphereData);
      }

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

      const paneStates = new Map<string, PaneGpuState>();

      const recreatePaneBindGroups = (pane: PaneGpuState) => {
        if (!pane.accumA || !pane.accumB) {
          return;
        }

        const computeLayout = computePipeline.getBindGroupLayout(0);
        pane.bindGroupAB = device.createBindGroup({
          layout: computeLayout,
          entries: [
            { binding: 0, resource: { buffer: pane.uniformBuffer } },
            { binding: 1, resource: { buffer: sphereBuffer } },
            { binding: 2, resource: pane.accumA.createView() },
            { binding: 3, resource: pane.accumB.createView() },
          ],
        });
        pane.bindGroupBA = device.createBindGroup({
          layout: computeLayout,
          entries: [
            { binding: 0, resource: { buffer: pane.uniformBuffer } },
            { binding: 1, resource: { buffer: sphereBuffer } },
            { binding: 2, resource: pane.accumB.createView() },
            { binding: 3, resource: pane.accumA.createView() },
          ],
        });

        const blitLayout = blitPipeline.getBindGroupLayout(0);
        pane.blitBindGroupA = device.createBindGroup({
          layout: blitLayout,
          entries: [
            { binding: 0, resource: pane.accumA.createView() },
            { binding: 1, resource: { buffer: pane.displayUniformBuffer } },
          ],
        });
        pane.blitBindGroupB = device.createBindGroup({
          layout: blitLayout,
          entries: [
            { binding: 0, resource: pane.accumB.createView() },
            { binding: 1, resource: { buffer: pane.displayUniformBuffer } },
          ],
        });
      };

      const allocatePaneTextures = (pane: PaneGpuState, nextWidth: number, nextHeight: number) => {
        destroyPaneTextures(pane);

        pane.accumA = device.createTexture({
          size: [nextWidth, nextHeight],
          format: "rgba32float",
          usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING,
        });
        pane.accumB = device.createTexture({
          size: [nextWidth, nextHeight],
          format: "rgba32float",
          usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING,
        });
        pane.width = nextWidth;
        pane.height = nextHeight;
        pane.pingPong = 0;
        pane.sampleIndex = 0;
        pane.frame = 0;
        recreatePaneBindGroups(pane);
      };

      const createPane = (
        viewport: RenderViewport,
        layout: { renderRect: PaneRect; displayRect: PaneRect },
      ) => {
        const pane: PaneGpuState = {
          id: viewport.id,
          label: viewport.label,
          renderRect: layout.renderRect,
          displayRect: layout.displayRect,
          splitPresentation:
            viewport.presentation === "split-left" || viewport.presentation === "split-right",
          width: 0,
          height: 0,
          uniformBuffer: device.createBuffer({
            size: uniformBufferSize,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
          }),
          displayUniformBuffer: device.createBuffer({
            size: displayUniformBufferSize,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
          }),
          pingPong: 0,
          sampleIndex: 0,
          frame: 0,
        };
        allocatePaneTextures(pane, layout.renderRect.width, layout.renderRect.height);
        return pane;
      };

      const syncPaneStates = (
        currentViewports: RenderViewport[],
        layouts: Array<{ renderRect: PaneRect; displayRect: PaneRect }>,
      ) => {
        const currentIds = new Set(currentViewports.map((viewport) => viewport.id));
        paneStates.forEach((pane, id) => {
          if (!currentIds.has(id)) {
            destroyPane(pane);
            paneStates.delete(id);
          }
        });

        currentViewports.forEach((viewport, index) => {
          const layout = layouts[index];
          let pane = paneStates.get(viewport.id);

          if (!pane) {
            pane = createPane(viewport, layout);
            paneStates.set(viewport.id, pane);
          }

          pane.label = viewport.label;
          pane.renderRect = layout.renderRect;
          pane.displayRect = layout.displayRect;
          pane.splitPresentation =
            viewport.presentation === "split-left" || viewport.presentation === "split-right";

          if (pane.width !== layout.renderRect.width || pane.height !== layout.renderRect.height) {
            allocatePaneTextures(pane, layout.renderRect.width, layout.renderRect.height);
            dirtyViewportIdsRef.current.add(pane.id);
          }
        });
      };

      const markAllPanesDirty = () => {
        viewportsRef.current.forEach((viewport) => dirtyViewportIdsRef.current.add(viewport.id));
      };

      const recreateAllPaneBindGroups = () => {
        paneStates.forEach((pane) => {
          recreatePaneBindGroups(pane);
          dirtyViewportIdsRef.current.add(pane.id);
        });
      };

      const resize = () => {
        const cssWidth = canvas.clientWidth;
        const cssHeight = canvas.clientHeight;
        let nextWidth = Math.max(2, Math.floor(cssWidth));
        let nextHeight = Math.max(2, Math.floor(cssHeight));
        const maxPixels = Math.min(
          ...viewportsRef.current.map((viewport) => viewport.settings.maxPixels),
        );
        const pixels = nextWidth * nextHeight;

        if (pixels > maxPixels) {
          const scale = Math.sqrt(maxPixels / pixels);
          nextWidth = Math.max(2, Math.floor(nextWidth * scale));
          nextHeight = Math.max(2, Math.floor(nextHeight * scale));
        }

        if (nextWidth === width && nextHeight === height) {
          return;
        }

        width = nextWidth;
        height = nextHeight;
        canvas.width = nextWidth;
        canvas.height = nextHeight;
        markAllPanesDirty();
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
        markAllPanesDirty();
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
        markAllPanesDirty();
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
      const displayUniformData = new Uint32Array(4);
      let fpsFrames = 0;
      let fpsTimer = performance.now();

      const writePaneUniforms = (
        pane: PaneGpuState,
        settings: RenderSettings,
        camera: CameraBasis,
      ) => {
        uniformFloats[0] = pane.width;
        uniformFloats[1] = pane.height;
        uniformInts[2] = pane.frame >>> 0;
        uniformInts[3] = settings.temporalAccumulation ? pane.sampleIndex >>> 0 : 0;
        uniformFloats[4] = camera.position[0];
        uniformFloats[5] = camera.position[1];
        uniformFloats[6] = camera.position[2];
        uniformFloats[7] = 0;
        uniformFloats[8] = camera.forward[0];
        uniformFloats[9] = camera.forward[1];
        uniformFloats[10] = camera.forward[2];
        uniformFloats[11] = 0;
        uniformFloats[12] = camera.right[0];
        uniformFloats[13] = camera.right[1];
        uniformFloats[14] = camera.right[2];
        uniformFloats[15] = 0;
        uniformFloats[16] = camera.up[0];
        uniformFloats[17] = camera.up[1];
        uniformFloats[18] = camera.up[2];
        uniformFloats[19] = (settings.fovDegrees * Math.PI) / 180;
        uniformInts[20] = cameraTypeIds[settings.cameraType] ?? 0;
        uniformFloats[21] = settings.orthoScale;
        uniformFloats[22] = settings.apertureRadius;
        uniformFloats[23] = settings.focusDistance;
        uniformInts[24] = settings.samplesPerDispatch >>> 0;
        uniformInts[25] = settings.maxBounces >>> 0;
        uniformInts[26] = 0;
        device.queue.writeBuffer(pane.uniformBuffer, 0, uniformData);

        displayUniformData[0] = toneMapIds[settings.toneMap] ?? 0;
        displayUniformData[1] = 0;
        displayUniformData[2] = 0;
        displayUniformData[3] = 0;
        device.queue.writeBuffer(pane.displayUniformBuffer, 0, displayUniformData);
      };

      const render = () => {
        if (cancelled) {
          return;
        }

        const currentViewports = viewportsRef.current;
        if (currentViewports.length === 0) {
          raf = requestAnimationFrame(render);
          return;
        }

        const layouts = layoutPanes(currentViewports, width, height);
        syncPaneStates(currentViewports, layouts);

        const nextSphereData = packSpheres(currentViewports[0].settings.sceneSpheres);
        if (nextSphereData.byteLength !== sphereBufferByteLength) {
          sphereBuffer.destroy();
          sphereBuffer = device.createBuffer({
            size: Math.max(4, nextSphereData.byteLength),
            usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
          });
          sphereBufferByteLength = nextSphereData.byteLength;
          recreateAllPaneBindGroups();
        }
        sphereData = nextSphereData;
        if (sphereData.byteLength > 0) {
          device.queue.writeBuffer(sphereBuffer, 0, sphereData);
        }

        const camera = buildCameraBasis(cameraRef.current);
        const encoder = device.createCommandEncoder();
        const computePass = encoder.beginComputePass();
        computePass.setPipeline(computePipeline);

        currentViewports.forEach((viewport) => {
          const pane = paneStates.get(viewport.id);
          if (!pane || !pane.bindGroupAB || !pane.bindGroupBA) {
            return;
          }

          if (dirtyViewportIdsRef.current.has(pane.id)) {
            pane.pingPong = 0;
            pane.sampleIndex = 0;
            pane.frame = 0;
            dirtyViewportIdsRef.current.delete(pane.id);
          }

          writePaneUniforms(pane, viewport.settings, camera);
          computePass.setBindGroup(0, pane.pingPong === 0 ? pane.bindGroupAB : pane.bindGroupBA);
          computePass.dispatchWorkgroups(Math.ceil(pane.width / 8), Math.ceil(pane.height / 8));
          pane.pingPong = pane.pingPong === 0 ? 1 : 0;
          pane.sampleIndex += 1;
          pane.frame += 1;
        });

        computePass.end();

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
        currentViewports.forEach((viewport) => {
          const pane = paneStates.get(viewport.id);
          const blitBindGroup =
            pane?.pingPong === 0 ? pane.blitBindGroupA : pane?.blitBindGroupB;

          if (!pane || !blitBindGroup) {
            return;
          }

          if (pane.splitPresentation) {
            renderPass.setViewport(0, 0, width, height, 0, 1);
          } else {
            renderPass.setViewport(
              pane.displayRect.x,
              pane.displayRect.y,
              pane.displayRect.width,
              pane.displayRect.height,
              0,
              1,
            );
          }
          renderPass.setScissorRect(
            pane.displayRect.x,
            pane.displayRect.y,
            pane.displayRect.width,
            pane.displayRect.height,
          );
          renderPass.setBindGroup(0, blitBindGroup);
          renderPass.draw(3);
        });
        renderPass.end();
        device.queue.submit([encoder.finish()]);

        fpsFrames += 1;

        const now = performance.now();
        if (now - fpsTimer > 500) {
          setStats({
            fps: (fpsFrames * 1000) / (now - fpsTimer),
            supported: true,
            width,
            height,
            panes: currentViewports.flatMap((viewport) => {
              const pane = paneStates.get(viewport.id);
              if (!pane) {
                return [];
              }

              return [
                {
                  id: pane.id,
                  label: pane.label,
                  samples: viewport.settings.temporalAccumulation
                    ? pane.sampleIndex * viewport.settings.samplesPerDispatch
                    : viewport.settings.samplesPerDispatch,
                  toneMap: viewport.settings.toneMap,
                  x: pane.displayRect.x,
                  y: pane.displayRect.y,
                  width: pane.displayRect.width,
                  height: pane.displayRect.height,
                },
              ];
            }),
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
        paneStates.forEach(destroyPane);
        sphereBuffer.destroy();
        device.destroy();
      });
    };

    setup().catch((error: unknown) => {
      setStats({
        fps: 0,
        supported: false,
        width: 0,
        height: 0,
        panes: [],
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
  const paneCount = stats.panes.length || viewports.length;

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
        <div className="pointer-events-none absolute inset-0">
          {paneCount > 1 ? (
            <div className="absolute inset-y-0 left-1/2 w-px bg-white/25 shadow-[0_0_0_1px_rgba(0,0,0,0.25)]" />
          ) : null}
          {stats.panes.map((pane) => (
            <div
              key={pane.id}
              className="absolute"
              style={{
                left: `${(pane.x / Math.max(1, stats.width)) * 100}%`,
                top: `${(pane.y / Math.max(1, stats.height)) * 100}%`,
                width: `${(pane.width / Math.max(1, stats.width)) * 100}%`,
                height: `${(pane.height / Math.max(1, stats.height)) * 100}%`,
              }}
            >
              <div className="absolute top-3 left-3 font-mono text-[11px] leading-4 font-medium tracking-wide text-white drop-shadow-[0_1px_2px_rgba(0,0,0,0.9)] uppercase tabular-nums">
                {paneCount > 1 ? (
                  <div className="mb-1 inline-flex rounded-sm bg-black/30 px-1.5 py-0.5 text-[10px] text-white/90 backdrop-blur-sm">
                    {pane.label}
                  </div>
                ) : null}
                <div>
                  FPS <span className={fpsTone}>{stats.fps.toFixed(1)}</span>
                </div>
                <div className="text-white/85">Samples {pane.samples}</div>
                <div className="text-white/85">{pane.toneMap}</div>
                {pane.width > 0 && pane.height > 0 ? (
                  <div className="text-white/85">
                    Render {pane.width} x {pane.height}
                  </div>
                ) : null}
              </div>
            </div>
          ))}
          {stats.panes.length === 0 ? (
            <div className="absolute top-3 left-3 font-mono text-[11px] leading-4 font-medium tracking-wide text-white drop-shadow-[0_1px_2px_rgba(0,0,0,0.9)] uppercase tabular-nums">
              <div>
                FPS <span className={fpsTone}>{stats.fps.toFixed(1)}</span>
              </div>
            </div>
          ) : null}
        </div>
      )}
      <div
        aria-hidden="true"
        className={cn(
          "pointer-events-none absolute inset-x-0 bottom-0 h-px bg-white/20",
          paneCount <= 1 && "hidden",
        )}
      />
    </div>
  );
}
