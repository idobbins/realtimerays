"use client";

import { useState } from "react";

import { AppSidebar } from "@/components/app-sidebar";
import { RenderScene } from "@/components/render-scene";
import { AspectRatio } from "@/components/ui/aspect-ratio";
import { SidebarInset, SidebarProvider } from "@/components/ui/sidebar";
import { Tabs, TabsList, TabsTrigger } from "@/components/ui/tabs";
import {
  defaultRenderSettings,
  renderAspectRatioProfiles,
  type RenderSettings,
  type RenderViewport,
} from "@/lib/render-settings";
import { cn } from "@/lib/utils";

type ComparePaneId = "a" | "b";

const comparePaneTabs: Array<{ value: ComparePaneId; label: string }> = [
  { value: "a", label: "A" },
  { value: "b", label: "B" },
];
const comparisonDefaults: Partial<RenderSettings> = {
  toneMap: "aces",
  samplesPerDispatch: 4,
  temporalAccumulation: false,
};
const sharedSettingKeys = new Set<keyof RenderSettings>([
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
  const [activeComparePane, setActiveComparePane] = useState<ComparePaneId>("a");
  const [comparisonOverrides, setComparisonOverrides] =
    useState<Partial<RenderSettings>>(comparisonDefaults);
  const comparisonSettings = withSharedSettings(
    { ...renderSettings, ...comparisonOverrides },
    renderSettings,
  );
  const sidebarSettings = activeComparePane === "b" ? comparisonSettings : renderSettings;
  const sidebarContextLabel = `Pane ${activeComparePane.toUpperCase()}`;
  const activeAspectRatio =
    renderAspectRatioProfiles.find((profile) => profile.value === renderSettings.renderAspectRatio) ??
    renderAspectRatioProfiles[0];
  const renderViewports: RenderViewport[] = [
    { id: "a", label: "A Reference", settings: renderSettings, presentation: "split-left" },
    { id: "b", label: "B Variant", settings: comparisonSettings, presentation: "split-right" },
  ];

  const updateActiveSetting = <Key extends keyof RenderSettings>(
    key: Key,
    value: RenderSettings[Key],
  ) => {
    if (activeComparePane === "b" && !sharedSettingKeys.has(key)) {
      setComparisonOverrides((current) => ({ ...current, [key]: value }));
      return;
    }

    setRenderSettings((current) => ({ ...current, [key]: value }));

    if (sharedSettingKeys.has(key)) {
      setComparisonOverrides((current) => {
        const next = { ...current };
        delete next[key];
        return next;
      });
    }
  };

  const resetActiveSettings = () => {
    if (activeComparePane === "b") {
      setComparisonOverrides(comparisonDefaults);
      return;
    }

    setRenderSettings(defaultRenderSettings);
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
        settings={sidebarSettings}
        contextLabel={sidebarContextLabel}
        onSettingChange={updateActiveSetting}
        onResetSettings={resetActiveSettings}
      />
      <SidebarInset className="min-w-0 bg-muted/60 p-2 pl-0 md:p-3 md:pt-2 md:pl-0">
        <div className="mb-2 flex h-10 shrink-0 items-center justify-end gap-1 pr-2">
          <Tabs
            value={activeComparePane}
            onValueChange={(value) => setActiveComparePane(value as ComparePaneId)}
            className="gap-0"
            aria-label="Edited comparison pane"
          >
            <TabsList className="h-8">
              {comparePaneTabs.map((tab) => (
                <TabsTrigger key={tab.value} value={tab.value} className="w-10 text-xs">
                  {tab.label}
                </TabsTrigger>
              ))}
            </TabsList>
          </Tabs>
        </div>
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
