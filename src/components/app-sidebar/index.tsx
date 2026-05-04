"use client";

import { useState } from "react";
import { RotateCcwIcon } from "lucide-react";
import { AnimatePresence, motion, useReducedMotion, type Variants } from "motion/react";

import {
  SharedSessionAccordion,
  VariantPipelineAccordion,
} from "@/components/app-sidebar/_components/render-pipeline-accordion";
import {
  SidebarHeaderActions,
  type RecordingState,
} from "@/components/app-sidebar/_components/sidebar-header-actions";
import { Button } from "@/components/ui/button";
import { ButtonGroup } from "@/components/ui/button-group";
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectLabel,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Sidebar, SidebarContent, SidebarHeader } from "@/components/ui/sidebar";
import { Switch } from "@/components/ui/switch";
import { Tabs, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { recordingProfiles, type RecordingProfileId } from "@/lib/recording-settings";
import type { ComparisonMode, ComparisonPaneId, RenderSettings } from "@/lib/render-settings";
import { cn } from "@/lib/utils";

import type { RenderSettingChange } from "./_components/sidebar-section";
import { SettingRow } from "./_components/setting-row";

type AppSidebarProps = {
  sharedSettings: RenderSettings;
  variantSettings: RenderSettings;
  contextLabel?: string;
  comparisonMode: ComparisonMode;
  activePaneId: ComparisonPaneId;
  onComparisonModeChange: (mode: ComparisonMode) => void;
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

const comparisonModeOptions: Array<{
  value: ComparisonMode;
  label: string;
  detail: string;
}> = [
  { value: "inline-split", label: "Inline", detail: "one framed view cut in half" },
  { value: "side-by-side", label: "Side", detail: "two independent panes" },
  { value: "swap", label: "Swap", detail: "show the selected variant" },
];

const comparePaneTabs: Array<{ value: ComparisonPaneId; label: string }> = [
  { value: "a", label: "A" },
  { value: "b", label: "B" },
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
  contextLabel = "Active",
  comparisonMode,
  activePaneId,
  onComparisonModeChange,
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

  return (
    <Sidebar collapsible="none" className="hidden h-svh border-r-0 bg-muted/60 md:flex">
      <SidebarHeader className="gap-3 px-3 pt-3 pb-3">
        <SidebarHeaderActions
          recordingState={recordingState}
          onToggleRecording={onToggleRecording}
          onTakeScreenshot={onTakeScreenshot}
        />
      </SidebarHeader>

      <SidebarContent>
        <div className="px-3 pb-4 pt-1">
          <div className="mb-2 px-2 text-xs text-muted-foreground">
            <span>Shared session</span>
          </div>

          <div className="mb-2 grid gap-2">
            <label
              htmlFor="render-enabled"
              className="flex min-h-8 items-center justify-between gap-3 rounded-md px-2 py-1 text-[12px] text-sidebar-foreground/80 transition-colors hover:bg-sidebar-accent/50"
            >
              <span className="grid min-w-0 gap-0.5">
                <span className="truncate">Rendering</span>
                <span className="truncate text-[10px] text-muted-foreground">
                  {renderEnabled ? "active" : "paused"}
                </span>
              </span>
              <Switch
                id="render-enabled"
                size="sm"
                checked={renderEnabled}
                onCheckedChange={onRenderEnabledChange}
                aria-label="Rendering"
              />
            </label>

            <label
              htmlFor="auto-orbit-camera"
              className="flex min-h-8 items-center justify-between gap-3 rounded-md px-2 py-1 text-[12px] text-sidebar-foreground/80 transition-colors hover:bg-sidebar-accent/50"
            >
              <span className="grid min-w-0 gap-0.5">
                <span className="truncate">Auto orbit camera</span>
                <span className="truncate text-[10px] text-muted-foreground">
                  {autoOrbit ? "rotating view" : "manual view"}
                </span>
              </span>
              <Switch
                id="auto-orbit-camera"
                size="sm"
                checked={autoOrbit}
                onCheckedChange={onAutoOrbitChange}
                aria-label="Auto orbit camera"
              />
            </label>

            <SettingRow label="recording quality">
              <Select
                value={recordingProfileId}
                onValueChange={(value) => onRecordingProfileChange(value as RecordingProfileId)}
                disabled={recordingState !== "idle"}
              >
                <SelectTrigger size="sm" className="h-7 w-36 bg-background/60 text-[11px]">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent align="end" alignItemWithTrigger={false} sideOffset={8}>
                  <SelectGroup>
                    <SelectLabel>Recording quality</SelectLabel>
                    {recordingProfiles.map((profile) => (
                      <SelectItem
                        key={profile.id}
                        value={profile.id}
                        title={profile.detail}
                        className="text-xs"
                      >
                        {profile.menuLabel}
                      </SelectItem>
                    ))}
                  </SelectGroup>
                </SelectContent>
              </Select>
            </SettingRow>
          </div>

          <SharedSessionAccordion
            settings={sharedSettings}
            onSettingChange={onSharedSettingChange}
          />

          <div className="mt-4 mb-2 flex items-center justify-between gap-2 px-2 text-xs text-muted-foreground">
            <span>Comparison</span>
            <span className="truncate font-mono text-[10px] tracking-wide uppercase">
              {contextLabel}
            </span>
          </div>

          <div className="mb-3 flex items-center justify-end px-2">
            <Tabs
              value={activePaneId}
              onValueChange={(value) => selectPane(value as ComparisonPaneId)}
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

          <div className="mb-3 grid gap-1.5 px-2">
            <div className="flex items-center justify-between gap-2 px-0.5">
              <span className="text-[11px] font-medium text-muted-foreground">View mode</span>
            </div>
            <ButtonGroup className="grid w-full grid-cols-3">
              {comparisonModeOptions.map((option) => {
                const selected = option.value === comparisonMode;

                return (
                  <Button
                    key={option.value}
                    type="button"
                    variant="outline"
                    size="sm"
                    aria-pressed={selected}
                    title={option.detail}
                    onClick={() => onComparisonModeChange(option.value)}
                    className={cn(
                      "h-8 min-w-0 bg-background/60 px-2 text-[11px] text-muted-foreground shadow-none",
                      selected &&
                        "border-sidebar-border bg-sidebar-accent text-sidebar-accent-foreground shadow-[inset_0_0_0_1px_var(--sidebar-border)]",
                    )}
                  >
                    <span className="truncate">{option.label}</span>
                  </Button>
                );
              })}
            </ButtonGroup>
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
            Reset active variant
          </Button>
        </div>
      </SidebarContent>
    </Sidebar>
  );
}
