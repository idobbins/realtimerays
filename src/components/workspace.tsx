"use client";

import { useRef, useState } from "react";

import { AppSidebar } from "@/components/app-sidebar";
import { RenderScene, type RenderSceneHandle } from "@/components/render-scene";
import { AspectRatio } from "@/components/ui/aspect-ratio";
import { SidebarInset, SidebarProvider } from "@/components/ui/sidebar";
import type { RecordingState } from "@/components/app-sidebar/_components/sidebar-header-actions";
import {
  defaultRecordingProfileId,
  getRecordingProfile,
  type RecordingProfile,
  type RecordingProfileId,
} from "@/lib/recording-settings";
import {
  defaultRenderSettings,
  renderAspectRatioProfiles,
  type ComparisonMode,
  type ComparisonPaneId,
  type RenderSettings,
  type RenderViewport,
} from "@/lib/render-settings";
import { cn } from "@/lib/utils";

const comparisonDefaults: Partial<RenderSettings> = {
  samplesPerDispatch: 4,
  temporalAccumulation: false,
};
const defaultVariantSettings = {
  samplesPerDispatch: defaultRenderSettings.samplesPerDispatch,
  maxBounces: defaultRenderSettings.maxBounces,
  temporalAccumulation: defaultRenderSettings.temporalAccumulation,
} satisfies Partial<RenderSettings>;
const sharedSettingKeys = new Set<keyof RenderSettings>([
  "cameraType",
  "fovDegrees",
  "orthoScale",
  "apertureRadius",
  "focusDistance",
  "scenePresetId",
  "sceneMaterialSeed",
  "sceneSpheres",
  "renderQuality",
  "renderAspectRatio",
  "toneMap",
  "maxPixels",
]);

type ActiveRecording = {
  recorder: MediaRecorder;
  stream: MediaStream;
  chunks: Blob[];
};

function createExportFilename(extension: string) {
  const stamp = new Date().toISOString().replaceAll(":", "-").replaceAll(".", "-");
  return `realtimerays-${stamp}.${extension}`;
}

function downloadBlob(blob: Blob, filename: string) {
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");

  link.href = url;
  link.download = filename;
  document.body.append(link);
  link.click();
  link.remove();
  window.setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function getRecordingMimeType(profile: RecordingProfile) {
  if (typeof MediaRecorder === "undefined") {
    return "";
  }

  return (
    profile.preferredMimeTypes.find((mimeType) => MediaRecorder.isTypeSupported(mimeType)) ?? ""
  );
}

function withSharedSettings(settings: RenderSettings, shared: RenderSettings): RenderSettings {
  return {
    ...settings,
    cameraType: shared.cameraType,
    fovDegrees: shared.fovDegrees,
    orthoScale: shared.orthoScale,
    apertureRadius: shared.apertureRadius,
    focusDistance: shared.focusDistance,
    scenePresetId: shared.scenePresetId,
    sceneMaterialSeed: shared.sceneMaterialSeed,
    sceneSpheres: shared.sceneSpheres,
    renderQuality: shared.renderQuality,
    renderAspectRatio: shared.renderAspectRatio,
    toneMap: shared.toneMap,
    maxPixels: shared.maxPixels,
  };
}

export function Workspace() {
  const renderSceneRef = useRef<RenderSceneHandle>(null);
  const recordingRef = useRef<ActiveRecording | null>(null);
  const [renderSettings, setRenderSettings] = useState<RenderSettings>(defaultRenderSettings);
  const [comparisonMode, setComparisonMode] = useState<ComparisonMode>("inline-split");
  const [activeComparePane, setActiveComparePane] = useState<ComparisonPaneId>("a");
  const [recordingState, setRecordingState] = useState<RecordingState>("idle");
  const [renderEnabled, setRenderEnabled] = useState(true);
  const [autoOrbit, setAutoOrbit] = useState(false);
  const [recordingProfileId, setRecordingProfileId] =
    useState<RecordingProfileId>(defaultRecordingProfileId);
  const [comparisonOverrides, setComparisonOverrides] =
    useState<Partial<RenderSettings>>(comparisonDefaults);
  const comparisonSettings = withSharedSettings(
    { ...renderSettings, ...comparisonOverrides },
    renderSettings,
  );
  const sidebarSettings = activeComparePane === "b" ? comparisonSettings : renderSettings;
  const sidebarContextLabel = `Pane ${activeComparePane.toUpperCase()}`;
  const activeAspectRatio =
    renderAspectRatioProfiles.find(
      (profile) => profile.value === renderSettings.renderAspectRatio,
    ) ?? renderAspectRatioProfiles[0];
  const renderViewports: RenderViewport[] =
    comparisonMode === "swap"
      ? [
          activeComparePane === "a"
            ? { id: "a", label: "A Reference", settings: renderSettings }
            : { id: "b", label: "B Variant", settings: comparisonSettings },
        ]
      : comparisonMode === "side-by-side"
        ? [
            { id: "a", label: "A Reference", settings: renderSettings },
            { id: "b", label: "B Variant", settings: comparisonSettings },
          ]
        : [
            { id: "a", label: "A Reference", settings: renderSettings, presentation: "split-left" },
            {
              id: "b",
              label: "B Variant",
              settings: comparisonSettings,
              presentation: "split-right",
            },
          ];

  const updateSharedSetting = <Key extends keyof RenderSettings>(
    key: Key,
    value: RenderSettings[Key],
  ) => {
    setRenderSettings((current) => ({ ...current, [key]: value }));

    if (sharedSettingKeys.has(key)) {
      setComparisonOverrides((current) => {
        const next = { ...current };
        delete next[key];
        return next;
      });
    }
  };

  const updateVariantSetting = <Key extends keyof RenderSettings>(
    key: Key,
    value: RenderSettings[Key],
  ) => {
    if (activeComparePane === "b") {
      setComparisonOverrides((current) => ({ ...current, [key]: value }));
      return;
    }

    setRenderSettings((current) => ({ ...current, [key]: value }));
  };

  const resetActiveVariantSettings = () => {
    if (activeComparePane === "b") {
      setComparisonOverrides(comparisonDefaults);
      return;
    }

    setRenderSettings((current) => ({ ...current, ...defaultVariantSettings }));
  };

  const takeScreenshot = async () => {
    const blob = await renderSceneRef.current?.capturePng();

    if (!blob) {
      throw new Error("Render canvas is not ready.");
    }

    downloadBlob(blob, createExportFilename("png"));
  };

  const stopRecording = () => {
    const activeRecording = recordingRef.current;

    if (!activeRecording || activeRecording.recorder.state === "inactive") {
      return;
    }

    setRecordingState("stopping");
    activeRecording.recorder.stop();
  };

  const startRecording = () => {
    const renderScene = renderSceneRef.current;

    if (!renderScene) {
      throw new Error("Render canvas is not ready.");
    }

    if (typeof MediaRecorder === "undefined") {
      throw new Error("MediaRecorder is not available in this browser.");
    }

    setRecordingState("starting");

    const recordingProfile = getRecordingProfile(recordingProfileId);
    const stream = renderScene.captureStream(recordingProfile.fps);
    const mimeType = getRecordingMimeType(recordingProfile);
    const recorder = new MediaRecorder(stream, {
      ...(mimeType ? { mimeType } : {}),
      videoBitsPerSecond: recordingProfile.videoBitsPerSecond,
    });
    const chunks: Blob[] = [];

    recorder.addEventListener("dataavailable", (event) => {
      if (event.data.size > 0) {
        chunks.push(event.data);
      }
    });

    recorder.addEventListener("error", (event) => {
      console.error("Recording failed.", event);
      stream.getTracks().forEach((track) => track.stop());
      recordingRef.current = null;
      setRecordingState("idle");
    });

    recorder.addEventListener("stop", () => {
      const type = recorder.mimeType || "video/webm";

      stream.getTracks().forEach((track) => track.stop());
      recordingRef.current = null;
      setRecordingState("idle");

      if (chunks.length > 0) {
        downloadBlob(new Blob(chunks, { type }), createExportFilename("webm"));
      }
    });

    recordingRef.current = { recorder, stream, chunks };
    recorder.start(1000);
    setRecordingState("recording");
  };

  const toggleRecording = () => {
    if (recordingState === "recording") {
      stopRecording();
      return;
    }

    if (recordingState !== "idle") {
      return;
    }

    try {
      startRecording();
    } catch (error) {
      console.error("Could not start recording.", error);
      recordingRef.current?.stream.getTracks().forEach((track) => track.stop());
      recordingRef.current = null;
      setRecordingState("idle");
    }
  };

  return (
    <SidebarProvider
      className="h-svh w-screen overflow-hidden"
      style={
        {
          "--sidebar-width": "20rem",
        } as React.CSSProperties
      }
    >
      <AppSidebar
        sharedSettings={renderSettings}
        variantSettings={sidebarSettings}
        contextLabel={sidebarContextLabel}
        comparisonMode={comparisonMode}
        activePaneId={activeComparePane}
        onComparisonModeChange={setComparisonMode}
        onActivePaneChange={setActiveComparePane}
        onSharedSettingChange={updateSharedSetting}
        onVariantSettingChange={updateVariantSetting}
        onResetVariantSettings={resetActiveVariantSettings}
        recordingState={recordingState}
        onToggleRecording={toggleRecording}
        onTakeScreenshot={takeScreenshot}
        recordingProfileId={recordingProfileId}
        onRecordingProfileChange={setRecordingProfileId}
        autoOrbit={autoOrbit}
        onAutoOrbitChange={setAutoOrbit}
        renderEnabled={renderEnabled}
        onRenderEnabledChange={setRenderEnabled}
      />
      <SidebarInset className="min-w-0 bg-muted/60 p-2 pl-0 md:p-3 md:pt-2 md:pl-0">
        <div
          className={cn(
            "min-h-0 w-full flex-1",
            activeAspectRatio.ratio && "flex items-center justify-center p-2 md:p-3",
          )}
          style={activeAspectRatio.ratio ? { containerType: "size" } : undefined}
        >
          {activeAspectRatio.ratio ? (
            <AspectRatio
              ratio={activeAspectRatio.ratio}
              className="max-h-full max-w-full overflow-hidden rounded-xl bg-background shadow-sm ring-1 ring-border/70"
              style={{
                width: `min(100cqw, calc(100cqh * ${activeAspectRatio.ratio}))`,
              }}
            >
              <section className="size-full min-h-0 overflow-hidden">
                <RenderScene
                  ref={renderSceneRef}
                  viewports={renderViewports}
                  autoOrbit={autoOrbit}
                  renderEnabled={renderEnabled}
                />
              </section>
            </AspectRatio>
          ) : (
            <div className="flex size-full min-h-0 flex-col overflow-hidden rounded-xl bg-background shadow-sm ring-1 ring-border/70">
              <section className="min-h-0 flex-1 overflow-hidden">
                <RenderScene
                  ref={renderSceneRef}
                  viewports={renderViewports}
                  autoOrbit={autoOrbit}
                  renderEnabled={renderEnabled}
                />
              </section>
            </div>
          )}
        </div>
      </SidebarInset>
    </SidebarProvider>
  );
}
