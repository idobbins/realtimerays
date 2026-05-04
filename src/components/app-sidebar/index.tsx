"use client";

import { RotateCcwIcon } from "lucide-react";

import { RenderPipelineAccordion } from "@/components/app-sidebar/_components/render-pipeline-accordion";
import { SidebarHeaderActions } from "@/components/app-sidebar/_components/sidebar-header-actions";
import { Button } from "@/components/ui/button";
import { Sidebar, SidebarContent, SidebarHeader } from "@/components/ui/sidebar";
import type { RenderSettings } from "@/lib/render-settings";

import type { RenderSettingChange } from "./_components/sidebar-section";

type AppSidebarProps = {
  settings: RenderSettings;
  contextLabel?: string;
  onSettingChange: RenderSettingChange<RenderSettings>;
  onResetSettings: () => void;
};

export function AppSidebar({
  settings,
  contextLabel = "Active",
  onSettingChange,
  onResetSettings,
}: AppSidebarProps) {
  return (
    <Sidebar collapsible="none" className="hidden h-svh border-r-0 bg-muted/60 md:flex">
      <SidebarHeader className="gap-3 px-3 pt-3 pb-3">
        <SidebarHeaderActions />
      </SidebarHeader>

      <SidebarContent>
        <div className="px-3 pb-4 pt-1">
          <div className="mb-2 flex items-center justify-between gap-2 px-2 text-xs text-muted-foreground">
            <span>Render pipeline</span>
            <span className="truncate font-mono text-[10px] tracking-wide uppercase">
              {contextLabel}
            </span>
          </div>

          <RenderPipelineAccordion settings={settings} onSettingChange={onSettingChange} />

          <Button
            type="button"
            variant="outline"
            size="sm"
            onClick={onResetSettings}
            className="mt-3 w-full justify-start rounded-md border-sidebar-border bg-sidebar text-xs text-sidebar-foreground/75 shadow-none hover:bg-sidebar-accent hover:text-sidebar-accent-foreground"
          >
            <RotateCcwIcon className="size-3.5" />
            Reset path tracer settings
          </Button>
        </div>
      </SidebarContent>
    </Sidebar>
  );
}
