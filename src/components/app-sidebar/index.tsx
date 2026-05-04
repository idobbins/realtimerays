"use client";

import { useState } from "react";
import { RotateCcwIcon } from "lucide-react";
import { AnimatePresence, motion, useReducedMotion, type Variants } from "motion/react";

import {
  SharedSessionAccordion,
  VariantPipelineAccordion,
} from "@/components/app-sidebar/_components/render-pipeline-accordion";
import {
  SidebarRuntimeActions,
  type RecordingState,
} from "@/components/app-sidebar/_components/sidebar-header-actions";
import { Button } from "@/components/ui/button";
import { Sidebar, SidebarContent, SidebarHeader } from "@/components/ui/sidebar";
import { Tabs, TabsList, TabsTrigger } from "@/components/ui/tabs";
import type { RecordingProfileId } from "@/lib/recording-settings";
import type { ComparisonPaneId, RenderSettings } from "@/lib/render-settings";

import type { RenderSettingChange } from "./_components/sidebar-section";

type AppSidebarProps = {
  sharedSettings: RenderSettings;
  variantSettings: RenderSettings;
  activePaneId: ComparisonPaneId;
  onActivePaneChange: (paneId: ComparisonPaneId) => void;
  onSharedSettingChange: RenderSettingChange<RenderSettings>;
  onVariantSettingChange: RenderSettingChange<RenderSettings>;
  onResetVariantSettings: () => void;
  recordingState: RecordingState;
  onToggleRecording: () => void | Promise<void>;
  onTakeScreenshot: () => void | Promise<void>;
  recordingProfileId: RecordingProfileId;
  onRecordingProfileChange: (profileId: RecordingProfileId) => void;
  autoOrbit: boolean;
  onAutoOrbitChange: (enabled: boolean) => void;
  renderEnabled: boolean;
  onRenderEnabledChange: (enabled: boolean) => void;
};

const comparePaneTabs: Array<{ value: ComparisonPaneId; label: string; shortLabel: string }> = [
  { value: "a", label: "Baseline", shortLabel: "A" },
  { value: "b", label: "Variant", shortLabel: "B" },
];
const variantPanelTransition = {
  duration: 0.18,
  ease: [0.645, 0.045, 0.355, 1],
} as const;
const variantPanelVariants: Variants = {
  enter: (direction: number) => ({
    opacity: 0,
    transform: `translate3d(${direction * 14}px, 0, 0)`,
  }),
  center: {
    opacity: 1,
    transform: "translate3d(0, 0, 0)",
  },
  exit: (direction: number) => ({
    opacity: 0,
    transform: `translate3d(${direction * -14}px, 0, 0)`,
  }),
};

export function AppSidebar({
  sharedSettings,
  variantSettings,
  activePaneId,
  onActivePaneChange,
  onSharedSettingChange,
  onVariantSettingChange,
  onResetVariantSettings,
  recordingState,
  onToggleRecording,
  onTakeScreenshot,
  recordingProfileId,
  onRecordingProfileChange,
  autoOrbit,
  onAutoOrbitChange,
  renderEnabled,
  onRenderEnabledChange,
}: AppSidebarProps) {
  const [animationDirection, setAnimationDirection] = useState(1);
  const shouldReduceMotion = useReducedMotion();
  const selectPane = (paneId: ComparisonPaneId) => {
    if (paneId === activePaneId) {
      return;
    }

    setAnimationDirection(paneId === "b" ? 1 : -1);
    onActivePaneChange(paneId);
  };
  const resetLabel =
    activePaneId === "b" ? "Reset variant sampling" : "Reset baseline sampling";

  return (
    <Sidebar collapsible="none" className="hidden h-svh border-r-0 bg-muted/60 md:flex">
      <SidebarHeader className="gap-3 px-3 pt-3 pb-3">
        <SidebarRuntimeActions
          recordingState={recordingState}
          onToggleRecording={onToggleRecording}
          onTakeScreenshot={onTakeScreenshot}
          autoOrbit={autoOrbit}
          onAutoOrbitChange={onAutoOrbitChange}
          renderEnabled={renderEnabled}
          onRenderEnabledChange={onRenderEnabledChange}
        />
      </SidebarHeader>

      <SidebarContent>
        <div className="px-3 pb-4 pt-1">
          <div className="mb-2 px-2 text-xs text-muted-foreground">
            <span>Session settings</span>
          </div>

          <SharedSessionAccordion
            settings={sharedSettings}
            onSettingChange={onSharedSettingChange}
            recordingState={recordingState}
            recordingProfileId={recordingProfileId}
            onRecordingProfileChange={onRecordingProfileChange}
          />

          <div className="mt-4 mb-3 px-2">
            <Tabs
              value={activePaneId}
              onValueChange={(value) => selectPane(value as ComparisonPaneId)}
              className="gap-0"
              aria-label="Edited comparison pane"
            >
              <TabsList className="grid h-8 w-full grid-cols-2">
                {comparePaneTabs.map((tab) => (
                  <TabsTrigger key={tab.value} value={tab.value} className="text-xs">
                    <span className="mr-1 font-mono text-[10px] text-muted-foreground">
                      {tab.shortLabel}
                    </span>
                    {tab.label}
                  </TabsTrigger>
                ))}
              </TabsList>
            </Tabs>
          </div>

          <div className="overflow-hidden">
            <AnimatePresence initial={false} mode="popLayout" custom={animationDirection}>
              <motion.div
                key={activePaneId}
                custom={animationDirection}
                variants={variantPanelVariants}
                initial={shouldReduceMotion ? false : "enter"}
                animate="center"
                exit={shouldReduceMotion ? "center" : "exit"}
                transition={shouldReduceMotion ? { duration: 0 } : variantPanelTransition}
                className="will-change-transform"
              >
                <VariantPipelineAccordion
                  settings={variantSettings}
                  onSettingChange={onVariantSettingChange}
                />
              </motion.div>
            </AnimatePresence>
          </div>

          <Button
            type="button"
            variant="outline"
            size="sm"
            onClick={onResetVariantSettings}
            className="mt-3 w-full justify-start rounded-md border-sidebar-border bg-sidebar text-xs text-sidebar-foreground/75 shadow-none hover:bg-sidebar-accent hover:text-sidebar-accent-foreground"
          >
            <RotateCcwIcon className="size-3.5" />
            {resetLabel}
          </Button>
        </div>
      </SidebarContent>
    </Sidebar>
  );
}
