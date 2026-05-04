"use client";

import { forwardRef, useEffect, useImperativeHandle, useRef, useState } from "react";

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
  frameMs: number;
  frameMsHistory: number[];
};

type Stats = {
  supported: boolean;
  width: number;
  height: number;
  panes: PaneStats[];
  error?: string;
};

export type RenderSceneHandle = {
  capturePng: () => Promise<Blob>;
  captureStream: (fps?: number) => MediaStream;
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
const autoOrbitRadiansPerSecond = 0.22;
const paneTimingAverageLimit = 12;
const paneTimingGraphLimit = 48;
const paneFrameTimeColors = ["oklch(0.88 0.17 86)", "oklch(0.79 0.18 206)"] as const;
const timingGraphHeight = 96;
const frameTimeLabelOffset = timingGraphHeight + 12;

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

function getPaneFrameTimeColor(index: number) {
  return paneFrameTimeColors[index % paneFrameTimeColors.length];
}

function getTimingRange(histories: number[][]) {
  const values = histories.flat();

  if (values.length === 0) {
    return { min: 0, max: 1 };
  }

  const min = Math.min(...values);
  const max = Math.max(...values);

  return { min, max: max === min ? min + 1 : max };
}

function historyToPoints(
  values: number[],
  width: number,
  height: number,
  range: { min: number; max: number },
) {
  const span = range.max - range.min || 1;
  const lastIndex = Math.max(1, values.length - 1);

  return values.map((value, index) => {
    const x = (index / lastIndex) * width;
    const y = ((value - range.min) / span) * height;
    return { x, y };
  });
}

function buildSmoothLinePath(points: Array<{ x: number; y: number }>) {
  if (points.length === 0) {
    return "";
  }

  if (points.length === 1) {
    return `M ${points[0].x.toFixed(2)} ${points[0].y.toFixed(2)}`;
  }

  const [firstPoint, ...restPoints] = points;
  const commands = [`M ${firstPoint.x.toFixed(2)} ${firstPoint.y.toFixed(2)}`];

  restPoints.forEach((point, index) => {
    const previous = points[index];
    const controlX = (previous.x + point.x) / 2;

    commands.push(
      `C ${controlX.toFixed(2)} ${previous.y.toFixed(2)}, ${controlX.toFixed(2)} ${point.y.toFixed(2)}, ${point.x.toFixed(2)} ${point.y.toFixed(2)}`,
    );
  });

  return commands.join(" ");
}

function buildTopAreaPath(
  values: number[],
  width: number,
  height: number,
  range: { min: number; max: number },
) {
  if (values.length === 0) {
    return { areaPath: "", linePath: "" };
  }

  const points = historyToPoints(values, width, height, range);
  const linePath = buildSmoothLinePath(points);

  return { areaPath: `${linePath} L ${width} 0 L 0 0 Z`, linePath };
}

function ComparisonTimingGraph({ panes }: { panes: PaneStats[] }) {
  const range = getTimingRange(panes.map((pane) => pane.frameMsHistory));

  if (panes.length < 2) {
    return null;
  }

  return (
    <div className="absolute inset-x-0 top-0 h-24 drop-shadow-[0_1px_1px_rgba(0,0,0,0.32)]">
      <svg
        aria-label="Comparative frame time history"
        role="img"
        viewBox={`0 0 100 ${timingGraphHeight}`}
        className="size-full"
        preserveAspectRatio="none"
      >
        {panes.map((pane, index) => {
          const { areaPath, linePath } = buildTopAreaPath(
            pane.frameMsHistory,
            100,
            timingGraphHeight,
            range,
          );
          const color = getPaneFrameTimeColor(index);

          return areaPath ? (
            <g key={pane.id}>
              <path d={areaPath} fill={color} fillOpacity="0.2" />
              <path
                d={linePath}
                fill="none"
                stroke={color}
                strokeLinecap="round"
                strokeLinejoin="round"
                strokeOpacity="0.9"
                strokeWidth="1.8"
                vectorEffect="non-scaling-stroke"
              />
            </g>
          ) : null;
        })}
      </svg>
    </div>
  );
}

function RenderDivider() {
  return (
    <div aria-hidden="true" className="absolute inset-y-0 left-1/2 w-px -translate-x-1/2">
      <div className="absolute inset-y-0 left-0 w-px bg-white" />
    </div>
  );
}

function PaneHud({ pane, stats, index }: { pane: PaneStats; stats: Stats; index: number }) {
  const color = getPaneFrameTimeColor(index);
  const alignRight = index % 2 === 1;

  return (
    <div
      className="absolute min-w-0"
      style={{
        left: `${(pane.x / Math.max(1, stats.width)) * 100}%`,
        top: `${(pane.y / Math.max(1, stats.height)) * 100}%`,
        width: `${(pane.width / Math.max(1, stats.width)) * 100}%`,
        height: `${(pane.height / Math.max(1, stats.height)) * 100}%`,
      }}
    >
      <div
        className={cn(
          "absolute max-w-[calc(100%-1.5rem)] font-sans text-[24px] leading-7 font-extrabold tracking-normal tabular-nums drop-shadow-[0_1px_1px_rgba(0,0,0,0.5)]",
          alignRight ? "right-3 text-right" : "left-3",
        )}
        style={{ top: `${frameTimeLabelOffset}px`, color }}
      >
        <span className="truncate">{pane.frameMs.toFixed(1)} ms</span>
      </div>
    </div>
  );
}

function RenderOverlay({ stats, paneCount }: { stats: Stats; paneCount: number }) {
  return (
    <div className="pointer-events-none absolute inset-0">
      <ComparisonTimingGraph panes={stats.panes} />
      {paneCount > 1 ? <RenderDivider /> : null}
      {stats.panes.map((pane, index) => (
        <PaneHud key={pane.id} pane={pane} stats={stats} index={index} />
      ))}
    </div>
  );
}

const recordingHudFont = '800 24px "Geist", "Geist Fallback", ui-sans-serif, system-ui, sans-serif';

function drawRecordingText(
  context: CanvasRenderingContext2D,
  text: string,
  x: number,
  y: number,
  color: string,
  align: CanvasTextAlign = "left",
) {
  context.save();
  context.font = recordingHudFont;
  context.textAlign = align;
  context.textBaseline = "top";
  context.fillStyle = color;
  context.shadowColor = "rgba(0, 0, 0, 0.5)";
  context.shadowBlur = 1;
  context.shadowOffsetY = 1;
  context.fillText(text, x, y);
  context.restore();
}

function drawRecordingTopArea(
  context: CanvasRenderingContext2D,
  values: number[],
  width: number,
  color: string,
  range: { min: number; max: number },
) {
  if (values.length === 0) {
    return;
  }

  const height = timingGraphHeight;
  const points = historyToPoints(values, width, height, range);

  context.save();
  context.fillStyle = color;
  context.globalAlpha = 0.2;
  context.beginPath();
  drawSmoothCanvasPath(context, points);

  context.lineTo(width, 0);
  context.lineTo(0, 0);
  context.closePath();
  context.fill();
  context.restore();

  context.save();
  context.strokeStyle = color;
  context.globalAlpha = 0.9;
  context.lineWidth = 1.8;
  context.lineCap = "round";
  context.lineJoin = "round";
  context.shadowColor = "rgba(0, 0, 0, 0.32)";
  context.shadowBlur = 1;
  context.shadowOffsetY = 1;
  context.beginPath();
  drawSmoothCanvasPath(context, points);
  context.stroke();
  context.restore();
}

function drawSmoothCanvasPath(
  context: CanvasRenderingContext2D,
  points: Array<{ x: number; y: number }>,
) {
  if (points.length === 0) {
    return;
  }

  context.moveTo(points[0].x, points[0].y);

  for (let index = 1; index < points.length; index += 1) {
    const previous = points[index - 1];
    const point = points[index];
    const controlX = (previous.x + point.x) / 2;

    context.bezierCurveTo(controlX, previous.y, controlX, point.y, point.x, point.y);
  }
}

function average(values: number[]) {
  if (values.length === 0) {
    return 0;
  }

  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function appendTimingSample(history: number[], sample: number, limit: number) {
  return [...history, sample].slice(-limit);
}

function ensureTimingHistory(history: number[] | undefined, sample: number) {
  return history && history.length > 0 ? history : [sample];
}

function drawRecordingHud(
  context: CanvasRenderingContext2D,
  stats: Stats,
  paneCount: number,
  width: number,
  height: number,
) {
  if (stats.panes.length > 1) {
    const range = getTimingRange(stats.panes.map((pane) => pane.frameMsHistory));

    stats.panes.forEach((pane, index) => {
      drawRecordingTopArea(
        context,
        pane.frameMsHistory,
        width,
        getPaneFrameTimeColor(index),
        range,
      );
    });
  }

  if (paneCount > 1) {
    const x = Math.round(width / 2) + 0.5;

    context.save();
    context.strokeStyle = "rgb(255, 255, 255)";
    context.lineWidth = 1;
    context.beginPath();
    context.moveTo(x, 0);
    context.lineTo(x, height);
    context.stroke();
    context.restore();
  }

  stats.panes.forEach((pane, index) => {
    const color = getPaneFrameTimeColor(index);
    const text = `${pane.frameMs.toFixed(1)} ms`;
    const alignRight = index % 2 === 1;
    const x = alignRight ? pane.x + pane.width - 12 : pane.x + 12;
    const y = pane.y + frameTimeLabelOffset;

    context.font = recordingHudFont;
    drawRecordingText(context, text, x, y, color, alignRight ? "right" : "left");
  });
}

function createCompositedCaptureStream(
  sourceCanvas: HTMLCanvasElement,
  fps: number,
  getStats: () => Stats,
  getPaneCount: () => number,
) {
  const compositeCanvas = document.createElement("canvas");
  const context = compositeCanvas.getContext("2d", { alpha: false });

  if (!context) {
    return sourceCanvas.captureStream(fps);
  }

  let raf = 0;
  let stopped = false;

  const draw = () => {
    if (stopped) {
      return;
    }

    const width = Math.max(1, sourceCanvas.width);
    const height = Math.max(1, sourceCanvas.height);

    if (compositeCanvas.width !== width || compositeCanvas.height !== height) {
      compositeCanvas.width = width;
      compositeCanvas.height = height;
    }

    const stats = getStats();

    context.drawImage(sourceCanvas, 0, 0, width, height);
    drawRecordingHud(context, stats, getPaneCount(), width, height);
    raf = requestAnimationFrame(draw);
  };

  draw();

  const stream = compositeCanvas.captureStream(fps);
  const stop = () => {
    if (stopped) {
      return;
    }

    stopped = true;
    cancelAnimationFrame(raf);
  };

  stream.getTracks().forEach((track) => {
    const stopTrack = track.stop.bind(track);
    track.stop = () => {
      stop();
      stopTrack();
    };
  });

  return stream;
}

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
    return [
      { renderRect: { x: 0, y: 0, width, height }, displayRect: { x: 0, y: 0, width, height } },
    ];
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

export const RenderScene = forwardRef<
  RenderSceneHandle,
  { viewports: RenderViewport[]; autoOrbit?: boolean; renderEnabled?: boolean }
>(function RenderScene({ viewports, autoOrbit = false, renderEnabled = true }, ref) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [stats, setStats] = useState<Stats>({
    supported: true,
    width: 0,
    height: 0,
    panes: [],
  });
  const statsRef = useRef(stats);
  const cameraRef = useRef({ yaw: 0.6, pitch: 0.15, dist: 7.5 });
  const viewportsRef = useRef(viewports);
  const viewportSignatureRef = useRef(new Map<string, string>());
  const dirtyViewportIdsRef = useRef(new Set(viewports.map((viewport) => viewport.id)));
  const resizeRef = useRef<(() => void) | null>(null);
  const autoOrbitRef = useRef(autoOrbit);
  const renderEnabledRef = useRef(renderEnabled);

  useImperativeHandle(
    ref,
    () => ({
      capturePng: () => {
        const canvas = canvasRef.current;

        if (!canvas) {
          return Promise.reject(new Error("Render canvas is not ready."));
        }

        return new Promise<Blob>((resolve, reject) => {
          canvas.toBlob((blob) => {
            if (!blob) {
              reject(new Error("Could not capture the render canvas."));
              return;
            }

            resolve(blob);
          }, "image/png");
        });
      },
      captureStream: (fps = 60) => {
        const canvas = canvasRef.current;

        if (!canvas) {
          throw new Error("Render canvas is not ready.");
        }

        return createCompositedCaptureStream(
          canvas,
          fps,
          () => statsRef.current,
          () => statsRef.current.panes.length || viewportsRef.current.length,
        );
      },
    }),
    [],
  );

  useEffect(() => {
    statsRef.current = stats;
  }, [stats]);

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
    autoOrbitRef.current = autoOrbit;
  }, [autoOrbit]);

  useEffect(() => {
    renderEnabledRef.current = renderEnabled;
  }, [renderEnabled]);

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
          supported: false,
          width: 0,
          height: 0,
          panes: [],
          error: "No GPU adapter found.",
        });
        return;
      }

      const supportsTimestampQueries = adapter.features.has("timestamp-query" as GPUFeatureName);
      const device = await adapter.requestDevice({
        requiredFeatures: supportsTimestampQueries ? (["timestamp-query"] as GPUFeatureName[]) : [],
      });

      if (cancelled) {
        device.destroy();
        return;
      }

      const context = canvas.getContext("webgpu");

      if (!context) {
        setStats({
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
      let statsFrames = 0;
      let statsTimer = performance.now();
      let previousFrameTime = statsTimer;
      let timingReadPending = false;
      const paneGpuFrameMs = new Map<string, number>();
      const paneTimingHistories = new Map<string, number[]>();

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

      const collectPaneStats = (currentViewports: RenderViewport[], fallbackFrameMs: number) =>
        currentViewports.flatMap((viewport) => {
          const pane = paneStates.get(viewport.id);
          if (!pane) {
            return [];
          }

          return [
            {
              id: pane.id,
              frameMs: paneGpuFrameMs.get(pane.id) ?? fallbackFrameMs,
              frameMsHistory: ensureTimingHistory(
                paneTimingHistories.get(pane.id),
                fallbackFrameMs,
              ),
              x: pane.displayRect.x,
              y: pane.displayRect.y,
              width: pane.displayRect.width,
              height: pane.displayRect.height,
            },
          ];
        });

      const readGpuTimings = async ({
        querySet,
        resolveBuffer,
        readBuffer,
        paneIds,
      }: {
        querySet: GPUQuerySet;
        resolveBuffer: GPUBuffer;
        readBuffer: GPUBuffer;
        paneIds: string[];
      }) => {
        try {
          await readBuffer.mapAsync(GPUMapMode.READ);
          const timestamps = new BigUint64Array(readBuffer.getMappedRange());

          paneIds.forEach((paneId, index) => {
            const queryIndex = index * 4;
            const computeStart = timestamps[queryIndex];
            const computeEnd = timestamps[queryIndex + 1];
            const blitStart = timestamps[queryIndex + 2];
            const blitEnd = timestamps[queryIndex + 3];

            if (computeEnd <= computeStart || blitEnd <= blitStart) {
              return;
            }

            const elapsedNanoseconds = computeEnd - computeStart + (blitEnd - blitStart);
            const sampleMs = Number(elapsedNanoseconds) / 1_000_000;
            const graphHistory = appendTimingSample(
              paneTimingHistories.get(paneId) ?? [],
              sampleMs,
              paneTimingGraphLimit,
            );
            const averageHistory = graphHistory.slice(-paneTimingAverageLimit);

            paneTimingHistories.set(paneId, graphHistory);
            paneGpuFrameMs.set(paneId, average(averageHistory));
          });

          readBuffer.unmap();
          setStats((current) => ({
            ...current,
            panes: current.panes.map((pane) => ({
              ...pane,
              frameMs: paneGpuFrameMs.get(pane.id) ?? pane.frameMs,
              frameMsHistory: paneTimingHistories.get(pane.id) ?? pane.frameMsHistory,
            })),
          }));
        } catch (error) {
          console.warn("Could not read WebGPU timestamp query results.", error);
        } finally {
          querySet.destroy();
          resolveBuffer.destroy();
          readBuffer.destroy();
          timingReadPending = false;
        }
      };

      const render = () => {
        if (cancelled) {
          return;
        }

        const now = performance.now();
        const deltaSeconds = Math.min(0.05, (now - previousFrameTime) / 1000);
        previousFrameTime = now;
        const currentViewports = viewportsRef.current;
        if (currentViewports.length === 0) {
          raf = requestAnimationFrame(render);
          return;
        }

        if (!renderEnabledRef.current) {
          if (now - statsTimer > 500) {
            statsFrames = 0;
            statsTimer = now;
          }

          raf = requestAnimationFrame(render);
          return;
        }

        if (autoOrbitRef.current && !dragging) {
          cameraRef.current.yaw += autoOrbitRadiansPerSecond * deltaSeconds;
          markAllPanesDirty();
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

        const shouldCollectStats = now - statsTimer > 500;
        const timedPaneIds =
          supportsTimestampQueries && shouldCollectStats && !timingReadPending
            ? currentViewports
                .map((viewport) => viewport.id)
                .filter((viewportId) => paneStates.has(viewportId))
            : [];
        const queryIndexByPaneId = new Map(
          timedPaneIds.map((paneId, index) => [paneId, index * 4]),
        );
        const timingQueryCount = timedPaneIds.length * 4;
        const timingQuerySet =
          timingQueryCount > 0
            ? device.createQuerySet({ type: "timestamp", count: timingQueryCount })
            : null;
        const timingResolveBuffer =
          timingQueryCount > 0
            ? device.createBuffer({
                size: timingQueryCount * BigUint64Array.BYTES_PER_ELEMENT,
                usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC,
              })
            : null;
        const timingReadBuffer =
          timingQueryCount > 0
            ? device.createBuffer({
                size: timingQueryCount * BigUint64Array.BYTES_PER_ELEMENT,
                usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
              })
            : null;

        if (timingQueryCount > 0) {
          timingReadPending = true;
        }

        const camera = buildCameraBasis(cameraRef.current);
        const encoder = device.createCommandEncoder();

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
          const queryIndex = queryIndexByPaneId.get(viewport.id);
          const computePassDescriptor: GPUComputePassDescriptor = {};
          if (timingQuerySet && queryIndex !== undefined) {
            computePassDescriptor.timestampWrites = {
              querySet: timingQuerySet,
              beginningOfPassWriteIndex: queryIndex,
              endOfPassWriteIndex: queryIndex + 1,
            };
          }
          const computePass = encoder.beginComputePass(computePassDescriptor);
          computePass.setPipeline(computePipeline);
          computePass.setBindGroup(0, pane.pingPong === 0 ? pane.bindGroupAB : pane.bindGroupBA);
          computePass.dispatchWorkgroups(Math.ceil(pane.width / 8), Math.ceil(pane.height / 8));
          computePass.end();
          pane.pingPong = pane.pingPong === 0 ? 1 : 0;
          pane.sampleIndex += 1;
          pane.frame += 1;
        });

        const frameTextureView = context.getCurrentTexture().createView();
        let shouldClearRenderTarget = true;
        currentViewports.forEach((viewport) => {
          const pane = paneStates.get(viewport.id);
          const blitBindGroup = pane?.pingPong === 0 ? pane.blitBindGroupA : pane?.blitBindGroupB;

          if (!pane || !blitBindGroup) {
            return;
          }

          const queryIndex = queryIndexByPaneId.get(viewport.id);
          const renderPassDescriptor: GPURenderPassDescriptor = {
            colorAttachments: [
              {
                view: frameTextureView,
                clearValue: { r: 0, g: 0, b: 0, a: 1 },
                loadOp: shouldClearRenderTarget ? "clear" : "load",
                storeOp: "store",
              },
            ],
          };
          if (timingQuerySet && queryIndex !== undefined) {
            renderPassDescriptor.timestampWrites = {
              querySet: timingQuerySet,
              beginningOfPassWriteIndex: queryIndex + 2,
              endOfPassWriteIndex: queryIndex + 3,
            };
          }
          const renderPass = encoder.beginRenderPass(renderPassDescriptor);
          shouldClearRenderTarget = false;
          renderPass.setPipeline(blitPipeline);
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
          renderPass.end();
        });

        if (timingQuerySet && timingResolveBuffer && timingReadBuffer) {
          encoder.resolveQuerySet(timingQuerySet, 0, timingQueryCount, timingResolveBuffer, 0);
          encoder.copyBufferToBuffer(
            timingResolveBuffer,
            0,
            timingReadBuffer,
            0,
            timingQueryCount * BigUint64Array.BYTES_PER_ELEMENT,
          );
        }

        device.queue.submit([encoder.finish()]);
        statsFrames += 1;

        if (timingQuerySet && timingResolveBuffer && timingReadBuffer) {
          void readGpuTimings({
            querySet: timingQuerySet,
            resolveBuffer: timingResolveBuffer,
            readBuffer: timingReadBuffer,
            paneIds: timedPaneIds,
          });
        }

        if (shouldCollectStats) {
          const measuredFrameMs = (now - statsTimer) / Math.max(1, statsFrames);

          setStats({
            supported: true,
            width,
            height,
            panes: collectPaneStats(currentViewports, measuredFrameMs),
          });
          statsFrames = 0;
          statsTimer = now;
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

  const paneCount = stats.panes.length || viewports.length;
  const showRenderSurface = stats.supported && renderEnabled;

  return (
    <div
      className="relative size-full overflow-hidden"
      aria-label="WebGPU path tracing render preview"
    >
      <canvas
        ref={canvasRef}
        className={cn(
          "block size-full cursor-grab touch-none active:cursor-grabbing",
          !showRenderSurface && "invisible",
        )}
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
      ) : showRenderSurface ? (
        <RenderOverlay stats={stats} paneCount={paneCount} />
      ) : null}
      <div
        aria-hidden="true"
        className={cn(
          "pointer-events-none absolute inset-x-0 bottom-0 h-px bg-white/20",
          (!showRenderSurface || paneCount <= 1) && "hidden",
        )}
      />
    </div>
  );
});
