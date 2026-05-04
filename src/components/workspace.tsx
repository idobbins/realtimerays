"use client";

import { useState } from "react";
import { PlusIcon } from "lucide-react";

import { AppSidebar } from "@/components/app-sidebar";
import { RenderScene } from "@/components/render-scene";
import { SidebarInset, SidebarProvider } from "@/components/ui/sidebar";
import { Tabs, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { defaultRenderSettings, type RenderSettings } from "@/lib/render-settings";

const workspaceTabs = [{ value: "active", label: "Active" }];

export function Workspace() {
  const [renderSettings, setRenderSettings] = useState<RenderSettings>(defaultRenderSettings);

  return (
    <SidebarProvider
      style={
        {
          "--sidebar-width": "20rem",
        } as React.CSSProperties
      }
    >
      <AppSidebar settings={renderSettings} onSettingsChange={setRenderSettings} />
      <SidebarInset className="bg-muted/60 p-2 pl-0 md:p-3 md:pt-2 md:pl-0">
        <div className="mb-2 flex h-10 shrink-0 items-center gap-1 pr-2">
          <Tabs
            defaultValue={workspaceTabs[0].value}
            className="gap-0"
            aria-label="Workspace views"
          >
            <TabsList variant="chrome">
              {workspaceTabs.map((tab) => (
                <TabsTrigger key={tab.value} value={tab.value}>
                  {tab.label}
                </TabsTrigger>
              ))}
            </TabsList>
          </Tabs>
          <button
            type="button"
            aria-label="New tab"
            className="inline-flex size-8 items-center justify-center rounded-md text-muted-foreground transition-colors hover:bg-muted hover:text-foreground focus-visible:ring-[3px] focus-visible:ring-ring/50 focus-visible:outline-1 focus-visible:outline-ring"
          >
            <PlusIcon className="size-3.5" />
          </button>
        </div>
        <div className="flex min-h-0 w-full flex-1 flex-col overflow-hidden rounded-xl bg-background shadow-sm ring-1 ring-border/70">
          <section className="min-h-0 flex-1">
            <RenderScene settings={renderSettings} />
          </section>
        </div>
      </SidebarInset>
    </SidebarProvider>
  );
}
