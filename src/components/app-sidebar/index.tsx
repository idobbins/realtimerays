"use client";

import { useState } from "react";
import {
  CameraIcon,
  ChevronDownIcon,
  CircleDotIcon,
  Layers3Icon,
  LogOutIcon,
  UserPlusIcon,
} from "lucide-react";

import { Accordion, AccordionContent, AccordionItem, AccordionTrigger } from "@/components/ui/accordion";
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
import { CameraModeInfo } from "@/components/app-sidebar/_components/camera-mode-info";
import { SettingGroups } from "@/components/app-sidebar/_components/setting-groups";
import {
  cameraModes,
  getInitialActiveOptions,
  getInitialSettingValues,
  pipelineSectionSettingGroups,
  pipelineSections,
} from "@/lib/pipeline-data";
import type { CameraSettingValue } from "@/lib/pipeline-types";
import { cn } from "@/lib/utils";

export function AppSidebar() {
  const [activeOptions, setActiveOptions] =
    useState<Record<string, string>>(getInitialActiveOptions);
  const [settingValues, setSettingValues] =
    useState<Record<string, CameraSettingValue>>(getInitialSettingValues);
  const [isRecording, setIsRecording] = useState(false);
  const [screenshotFlashKey, setScreenshotFlashKey] = useState(0);

  const updateSetting = (settingId: string, value: CameraSettingValue) => {
    setSettingValues((current) => ({
      ...current,
      [settingId]: value,
    }));
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

          <Accordion multiple defaultValue={["camera"]}>
            {pipelineSections.map((section) => {
              const activeOption = activeOptions[section.id];
              const activeCameraMode =
                section.id === "camera"
                  ? (cameraModes.find((mode) => mode.label === activeOption) ?? cameraModes[0])
                  : undefined;
              const settingGroups =
                activeCameraMode?.settingGroups ?? pipelineSectionSettingGroups[section.id] ?? [];

              return (
                <AccordionItem
                  key={section.id}
                  value={section.id}
                  className="border-b border-sidebar-border/70 last:border-b-0"
                >
                  <AccordionTrigger className="min-h-9 gap-2 rounded-md px-2 py-1.5 text-sidebar-foreground hover:bg-sidebar-accent hover:no-underline">
                    <span className="flex min-w-0 flex-1 items-center gap-2">
                      <section.icon className="size-3.5 shrink-0 text-sidebar-foreground/55" />
                      <span className="min-w-0 flex-1 truncate text-[13px]">{section.title}</span>
                      <span className="max-w-34 truncate text-[11px] font-normal text-muted-foreground">
                        {activeOption}
                      </span>
                    </span>
                  </AccordionTrigger>
                  <AccordionContent className="space-y-3 px-2 pb-3">
                    {activeCameraMode ? (
                      <>
                        <div className="grid gap-1.5">
                          <span className="text-[11px] font-medium text-muted-foreground">
                            Camera type
                          </span>
                          <DropdownMenu>
                            <DropdownMenuTrigger
                              render={
                                <button
                                  type="button"
                                  className="flex min-h-10 w-full cursor-pointer items-center gap-2 rounded-md border border-sidebar-border bg-background/75 px-2 py-1.5 text-left text-sidebar-foreground transition-colors hover:bg-sidebar-accent hover:text-sidebar-accent-foreground focus-visible:border-ring focus-visible:outline-none"
                                />
                              }
                            >
                              <span className="flex min-w-0 flex-1 items-center gap-1.5">
                                <span className="min-w-0 truncate text-[12px] font-medium">
                                  {activeCameraMode.label}
                                </span>
                                <CameraModeInfo description={activeCameraMode.description} />
                              </span>
                              <ChevronDownIcon className="size-3.5 shrink-0 text-muted-foreground" />
                            </DropdownMenuTrigger>
                            <DropdownMenuContent align="start" className="max-h-80 w-72">
                              <DropdownMenuGroup>
                                {cameraModes.map((mode) => {
                                  const selected = activeOption === mode.label;

                                  return (
                                    <DropdownMenuItem
                                      key={mode.label}
                                      data-selected={selected}
                                      onClick={() => {
                                        setActiveOptions((current) => ({
                                          ...current,
                                          [section.id]: mode.label,
                                        }));
                                      }}
                                      className="gap-2 py-1.5 text-xs data-[selected=true]:bg-accent/70 data-[selected=true]:text-accent-foreground"
                                    >
                                      <span
                                        aria-hidden="true"
                                        data-selected={selected}
                                        className="size-1.5 shrink-0 rounded-full bg-transparent data-[selected=true]:bg-current"
                                      />
                                      <span className="flex min-w-0 flex-1 items-center gap-1.5">
                                        <span className="min-w-0 truncate font-medium">
                                          {mode.label}
                                        </span>
                                        <CameraModeInfo description={mode.description} />
                                      </span>
                                    </DropdownMenuItem>
                                  );
                                })}
                              </DropdownMenuGroup>
                            </DropdownMenuContent>
                          </DropdownMenu>
                        </div>

                        <SettingGroups
                          groups={settingGroups}
                          values={settingValues}
                          onSettingChange={updateSetting}
                        />
                      </>
                    ) : (
                      <>
                        <div className="grid gap-1.5">
                          <span className="text-[11px] font-medium text-muted-foreground">
                            {section.activeLabel}
                          </span>
                          <Select
                            value={activeOption}
                            onValueChange={(option) => {
                              if (option === null) {
                                return;
                              }

                              setActiveOptions((current) => ({
                                ...current,
                                [section.id]: option,
                              }));
                            }}
                          >
                            <SelectTrigger size="sm" className="w-full bg-background/60 text-xs">
                              <SelectValue />
                            </SelectTrigger>
                            <SelectContent align="start" className="max-h-72">
                              <SelectGroup>
                                <SelectLabel>{section.activeLabel}</SelectLabel>
                                {section.selectOptions.map((option) => (
                                  <SelectItem key={option} value={option} className="text-xs">
                                    {option}
                                  </SelectItem>
                                ))}
                              </SelectGroup>
                            </SelectContent>
                          </Select>
                        </div>

                        <SettingGroups
                          groups={settingGroups}
                          values={settingValues}
                          onSettingChange={updateSetting}
                        />
                      </>
                    )}
                  </AccordionContent>
                </AccordionItem>
              );
            })}
          </Accordion>
        </div>
      </SidebarContent>
    </Sidebar>
  );
}
