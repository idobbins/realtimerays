"use client";

import type { Dispatch, SetStateAction } from "react";
import { RotateCcwIcon } from "lucide-react";

import { RenderPipelineAccordion } from "@/components/app-sidebar/_components/render-pipeline-accordion";
import { SidebarHeaderActions } from "@/components/app-sidebar/_components/sidebar-header-actions";
import { Button } from "@/components/ui/button";
import { Sidebar, SidebarContent, SidebarHeader } from "@/components/ui/sidebar";
import { defaultRenderSettings, type RenderSettings } from "@/lib/render-settings";

type AppSidebarProps = {
  settings: RenderSettings;
  onSettingsChange: Dispatch<SetStateAction<RenderSettings>>;
};

export function AppSidebar({ settings, onSettingsChange }: AppSidebarProps) {
  const updateSetting = <Key extends keyof RenderSettings>(
    key: Key,
    value: RenderSettings[Key],
  ) => {
    onSettingsChange((current) => ({ ...current, [key]: value }));
  };

  return (
    <Sidebar collapsible="none" className="hidden h-svh border-r-0 bg-muted/60 md:flex">
      <SidebarHeader className="gap-3 px-3 pt-3 pb-3">
        <SidebarHeaderActions />
      </SidebarHeader>

      <SidebarContent>
        <div className="px-3 pb-4 pt-1">
          <div className="mb-2 px-2 text-xs text-muted-foreground">
            <span>Render pipeline</span>
          </div>

          <RenderPipelineAccordion settings={settings} onSettingChange={updateSetting} />

          <Button
            type="button"
            variant="outline"
            size="sm"
            onClick={() => onSettingsChange(defaultRenderSettings)}
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
