"use client";

import { useState } from "react";

import { AppSidebar } from "@/components/app-sidebar";
import { RenderScene } from "@/components/render-scene";
import { AspectRatio } from "@/components/ui/aspect-ratio";
import { SidebarInset, SidebarProvider } from "@/components/ui/sidebar";
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
  toneMap: "aces",
  samplesPerDispatch: 4,
  temporalAccumulation: false,
};
const defaultVariantSettings = {
  samplesPerDispatch: defaultRenderSettings.samplesPerDispatch,
  maxBounces: defaultRenderSettings.maxBounces,
  temporalAccumulation: defaultRenderSettings.temporalAccumulation,
  toneMap: defaultRenderSettings.toneMap,
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
  "maxPixels",
]);

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
    maxPixels: shared.maxPixels,
  };
}

export function Workspace() {
  const [renderSettings, setRenderSettings] = useState<RenderSettings>(defaultRenderSettings);
  const [comparisonMode, setComparisonMode] = useState<ComparisonMode>("inline-split");
  const [activeComparePane, setActiveComparePane] = useState<ComparisonPaneId>("a");
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
                <RenderScene viewports={renderViewports} />
              </section>
            </AspectRatio>
          ) : (
            <div className="flex size-full min-h-0 flex-col overflow-hidden rounded-xl bg-background shadow-sm ring-1 ring-border/70">
              <section className="min-h-0 flex-1 overflow-hidden">
                <RenderScene viewports={renderViewports} />
              </section>
            </div>
          )}
        </div>
      </SidebarInset>
    </SidebarProvider>
  );
}
