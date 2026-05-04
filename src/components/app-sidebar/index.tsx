"use client";

import { useState, type Dispatch, type SetStateAction } from "react";
import {
  ApertureIcon,
  BoxesIcon,
  CameraIcon,
  ChevronDownIcon,
  CircleDotIcon,
  ContrastIcon,
  GaugeIcon,
  Layers3Icon,
  LogOutIcon,
  RotateCcwIcon,
  SettingsIcon,
  SparklesIcon,
  UserPlusIcon,
} from "lucide-react";

import {
  Accordion,
  AccordionContent,
  AccordionItem,
  AccordionTrigger,
} from "@/components/ui/accordion";
import { Avatar, AvatarFallback } from "@/components/ui/avatar";
import { Button } from "@/components/ui/button";
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuGroup,
  DropdownMenuItem,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu";
import { Input } from "@/components/ui/input";
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectLabel,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import {
  Sidebar,
  SidebarContent,
  SidebarHeader,
  SidebarMenu,
  SidebarMenuButton,
  SidebarMenuItem,
} from "@/components/ui/sidebar";
import { defaultRenderSettings, type RenderSettings } from "@/lib/render-settings";
import { cn } from "@/lib/utils";

const pixelBudgetOptions = [
  { label: "0.4 MP", value: 400_000 },
  { label: "0.8 MP", value: 800_000 },
  { label: "1.6 MP", value: 1_600_000 },
  { label: "2.4 MP", value: 2_400_000 },
];

type AppSidebarProps = {
  settings: RenderSettings;
  onSettingsChange: Dispatch<SetStateAction<RenderSettings>>;
};

function SettingRow({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="flex min-h-8 items-center justify-between gap-3 rounded-md px-2 py-1 text-[12px] text-sidebar-foreground/80 transition-colors hover:bg-sidebar-accent/60">
      <span className="min-w-0 flex-1 truncate" title={label}>
        {label}
      </span>
      {children}
    </div>
  );
}

function Readout({ children }: { children: React.ReactNode }) {
  return (
    <span className="max-w-34 truncate text-right text-[11px] text-muted-foreground">
      {children}
    </span>
  );
}

export function AppSidebar({ settings, onSettingsChange }: AppSidebarProps) {
  const [isRecording, setIsRecording] = useState(false);
  const [screenshotFlashKey, setScreenshotFlashKey] = useState(0);

  const updateSetting = <Key extends keyof RenderSettings>(
    key: Key,
    value: RenderSettings[Key],
  ) => {
    onSettingsChange((current) => ({ ...current, [key]: value }));
  };

  return (
    <Sidebar collapsible="none" className="hidden h-svh border-r-0 bg-muted/60 md:flex">
      <SidebarHeader className="gap-3 px-3 pt-3 pb-3">
        <SidebarMenu className="flex-row items-center gap-1">
          <SidebarMenuItem className="min-w-0">
            <DropdownMenu>
              <DropdownMenuTrigger
                render={
                  <SidebarMenuButton
                    size="lg"
                    className="h-8 w-auto max-w-[9.5rem] gap-2 rounded-md px-1.5 pr-2 data-open:bg-sidebar-accent data-open:text-sidebar-accent-foreground"
                  />
                }
              >
                <Avatar size="sm">
                  <AvatarFallback>ID</AvatarFallback>
                </Avatar>
                <span className="truncate text-sm font-medium">idobbins</span>
                <ChevronDownIcon className="ml-0.5 size-3.5 text-sidebar-foreground/50" />
              </DropdownMenuTrigger>
              <DropdownMenuContent side="bottom" align="start" className="w-48 p-1.5">
                <DropdownMenuGroup>
                  <DropdownMenuItem>
                    <SettingsIcon />
                    <span>User settings</span>
                  </DropdownMenuItem>
                  <DropdownMenuItem>
                    <Layers3Icon />
                    <span>Pipeline presets</span>
                  </DropdownMenuItem>
                  <DropdownMenuItem>
                    <UserPlusIcon />
                    <span>Invite people</span>
                  </DropdownMenuItem>
                </DropdownMenuGroup>
                <DropdownMenuSeparator />
                <DropdownMenuGroup>
                  <DropdownMenuItem variant="destructive">
                    <LogOutIcon />
                    <span>Log out</span>
                  </DropdownMenuItem>
                </DropdownMenuGroup>
              </DropdownMenuContent>
            </DropdownMenu>
          </SidebarMenuItem>
          <SidebarMenuItem className="ml-auto shrink-0">
            <Button
              type="button"
              variant="outline"
              size="icon"
              aria-label={isRecording ? "Stop recording" : "Record"}
              aria-pressed={isRecording}
              title={isRecording ? "Stop recording" : "Record"}
              data-recording={isRecording}
              onClick={() => setIsRecording((recording) => !recording)}
              className={cn(
                "record-button relative overflow-hidden rounded-md border-sidebar-border bg-sidebar text-sidebar-foreground/70 shadow-none hover:bg-sidebar-accent hover:text-sidebar-accent-foreground",
                isRecording &&
                  "border-destructive/40 bg-destructive/10 text-destructive hover:bg-destructive/15 hover:text-destructive",
              )}
            >
              <CircleDotIcon className={cn("size-4", isRecording && "fill-current")} />
            </Button>
          </SidebarMenuItem>
          <SidebarMenuItem className="shrink-0">
            <Button
              type="button"
              variant="outline"
              size="icon"
              aria-label="Screenshot"
              title="Screenshot"
              onClick={() => setScreenshotFlashKey((key) => key + 1)}
              data-flashing={screenshotFlashKey > 0}
              className={cn(
                "relative overflow-hidden rounded-md border-sidebar-border bg-sidebar text-sidebar-foreground/70 shadow-none hover:bg-sidebar-accent hover:text-sidebar-accent-foreground",
                screenshotFlashKey > 0 &&
                  "border-blue-500/35 bg-blue-500/10 text-blue-600 hover:bg-blue-500/15 hover:text-blue-700 dark:text-blue-400 dark:hover:text-blue-300",
              )}
            >
              <CameraIcon className="size-4" />
              {screenshotFlashKey > 0 ? (
                <span
                  key={screenshotFlashKey}
                  aria-hidden="true"
                  className="screenshot-shimmer"
                  onAnimationEnd={() => setScreenshotFlashKey(0)}
                />
              ) : null}
            </Button>
          </SidebarMenuItem>
        </SidebarMenu>
      </SidebarHeader>

      <SidebarContent>
        <div className="px-3 pb-4 pt-1">
          <div className="mb-2 px-2 text-xs text-muted-foreground">
            <span>Render pipeline</span>
          </div>

          <Accordion multiple defaultValue={["camera", "render"]}>
            <AccordionItem value="camera" className="border-b border-sidebar-border/70">
              <AccordionTrigger className="min-h-9 gap-2 rounded-md px-2 py-1.5 text-sidebar-foreground hover:bg-sidebar-accent hover:no-underline">
                <span className="flex min-w-0 flex-1 items-center gap-2">
                  <ApertureIcon className="size-3.5 shrink-0 text-sidebar-foreground/55" />
                  <span className="min-w-0 flex-1 truncate text-[13px]">Camera</span>
                  <span className="max-w-34 truncate text-[11px] font-normal text-muted-foreground">
                    Pinhole
                  </span>
                </span>
              </AccordionTrigger>
              <AccordionContent className="space-y-3 px-2 pb-3">
                <div className="grid gap-2">
                  <div className="flex items-center justify-between gap-2">
                    <span className="text-[10px] font-medium tracking-wide text-muted-foreground uppercase">
                      Uniforms
                    </span>
                    <span className="text-[10px] text-muted-foreground">cam + fov</span>
                  </div>
                  <SettingRow label="camera type">
                    <Readout>pinhole</Readout>
                  </SettingRow>
                  <SettingRow label="vertical field of view">
                    <span className="flex shrink-0 items-center gap-1">
                      <Input
                        type="number"
                        min={20}
                        max={120}
                        step={1}
                        value={settings.fovDegrees}
                        onChange={(event) => {
                          updateSetting(
                            "fovDegrees",
                            Math.max(20, Math.min(120, Number(event.currentTarget.value) || 20)),
                          );
                        }}
                        className="h-7 w-20 bg-background/60 px-2 text-right text-[11px]"
                        aria-label="Vertical field of view"
                      />
                      <span className="w-6 text-[10px] text-muted-foreground">deg</span>
                    </span>
                  </SettingRow>
                  <SettingRow label="camera pose">
                    <Readout>orbit viewport</Readout>
                  </SettingRow>
                </div>
              </AccordionContent>
            </AccordionItem>

            <AccordionItem value="scene" className="border-b border-sidebar-border/70">
              <AccordionTrigger className="min-h-9 gap-2 rounded-md px-2 py-1.5 text-sidebar-foreground hover:bg-sidebar-accent hover:no-underline">
                <span className="flex min-w-0 flex-1 items-center gap-2">
                  <BoxesIcon className="size-3.5 shrink-0 text-sidebar-foreground/55" />
                  <span className="min-w-0 flex-1 truncate text-[13px]">Scene</span>
                  <span className="max-w-34 truncate text-[11px] font-normal text-muted-foreground">
                    fixed spheres
                  </span>
                </span>
              </AccordionTrigger>
              <AccordionContent className="space-y-3 px-2 pb-3">
                <div className="grid gap-2">
                  <div className="flex items-center justify-between gap-2">
                    <span className="text-[10px] font-medium tracking-wide text-muted-foreground uppercase">
                      Shader storage
                    </span>
                    <span className="text-[10px] text-muted-foreground">read-only</span>
                  </div>
                  <SettingRow label="geometry">
                    <Readout>6 spheres + plane</Readout>
                  </SettingRow>
                  <SettingRow label="materials">
                    <Readout>diffuse, metal, glass, emissive</Readout>
                  </SettingRow>
                  <SettingRow label="sky light">
                    <Readout>fixed gradient sun</Readout>
                  </SettingRow>
                </div>
              </AccordionContent>
            </AccordionItem>

            <AccordionItem value="sampling" className="border-b border-sidebar-border/70">
              <AccordionTrigger className="min-h-9 gap-2 rounded-md px-2 py-1.5 text-sidebar-foreground hover:bg-sidebar-accent hover:no-underline">
                <span className="flex min-w-0 flex-1 items-center gap-2">
                  <SparklesIcon className="size-3.5 shrink-0 text-sidebar-foreground/55" />
                  <span className="min-w-0 flex-1 truncate text-[13px]">Sampling</span>
                  <span className="max-w-34 truncate text-[11px] font-normal text-muted-foreground">
                    progressive
                  </span>
                </span>
              </AccordionTrigger>
              <AccordionContent className="space-y-3 px-2 pb-3">
                <div className="grid gap-2">
                  <div className="flex items-center justify-between gap-2">
                    <span className="text-[10px] font-medium tracking-wide text-muted-foreground uppercase">
                      Path tracer
                    </span>
                    <span className="text-[10px] text-muted-foreground">hardcoded</span>
                  </div>
                  <SettingRow label="samples per dispatch">
                    <Readout>1 spp</Readout>
                  </SettingRow>
                  <SettingRow label="max bounces">
                    <Readout>4</Readout>
                  </SettingRow>
                  <SettingRow label="accumulation">
                    <Readout>progressive average</Readout>
                  </SettingRow>
                </div>
              </AccordionContent>
            </AccordionItem>

            <AccordionItem
              value="render"
              className="border-b border-sidebar-border/70 last:border-b-0"
            >
              <AccordionTrigger className="min-h-9 gap-2 rounded-md px-2 py-1.5 text-sidebar-foreground hover:bg-sidebar-accent hover:no-underline">
                <span className="flex min-w-0 flex-1 items-center gap-2">
                  <GaugeIcon className="size-3.5 shrink-0 text-sidebar-foreground/55" />
                  <span className="min-w-0 flex-1 truncate text-[13px]">Render Output</span>
                  <span className="max-w-34 truncate text-[11px] font-normal text-muted-foreground">
                    {pixelBudgetOptions.find((option) => option.value === settings.maxPixels)
                      ?.label ?? `${Math.round(settings.maxPixels / 1000)}k px`}
                  </span>
                </span>
              </AccordionTrigger>
              <AccordionContent className="space-y-3 px-2 pb-3">
                <div className="grid gap-2">
                  <div className="flex items-center justify-between gap-2">
                    <span className="text-[10px] font-medium tracking-wide text-muted-foreground uppercase">
                      Target
                    </span>
                    <span className="text-[10px] text-muted-foreground">renderer config</span>
                  </div>
                  <SettingRow label="render pixel budget">
                    <Select
                      value={String(settings.maxPixels)}
                      onValueChange={(value) => updateSetting("maxPixels", Number(value))}
                    >
                      <SelectTrigger size="sm" className="h-7 w-24 bg-background/60 text-[11px]">
                        <SelectValue />
                      </SelectTrigger>
                      <SelectContent align="end">
                        <SelectGroup>
                          <SelectLabel>Render pixel budget</SelectLabel>
                          {pixelBudgetOptions.map((option) => (
                            <SelectItem
                              key={option.value}
                              value={String(option.value)}
                              className="text-xs"
                            >
                              {option.label}
                            </SelectItem>
                          ))}
                        </SelectGroup>
                      </SelectContent>
                    </Select>
                  </SettingRow>
                  <SettingRow label="tone map">
                    <span className="inline-flex items-center gap-1 text-[11px] text-muted-foreground">
                      <ContrastIcon className="size-3" />
                      Reinhard + gamma
                    </span>
                  </SettingRow>
                </div>
              </AccordionContent>
            </AccordionItem>
          </Accordion>

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
