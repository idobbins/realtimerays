"use client";

import { useState } from "react";
import {
  CameraIcon,
  ChevronDownIcon,
  CircleDotIcon,
  Layers3Icon,
  LogOutIcon,
  PauseIcon,
  PlayIcon,
  RotateCwIcon,
  SettingsIcon,
  UserPlusIcon,
} from "lucide-react";

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
import { SidebarMenu, SidebarMenuButton, SidebarMenuItem } from "@/components/ui/sidebar";
import { cn } from "@/lib/utils";

export type RecordingState = "idle" | "starting" | "recording" | "stopping";

type SidebarHeaderActionsProps = {
  recordingState: RecordingState;
  onToggleRecording: () => void | Promise<void>;
  onTakeScreenshot: () => void | Promise<void>;
};

type SidebarRuntimeActionsProps = SidebarHeaderActionsProps & {
  autoOrbit: boolean;
  onAutoOrbitChange: (enabled: boolean) => void;
  renderEnabled: boolean;
  onRenderEnabledChange: (enabled: boolean) => void;
};

export function SidebarUserMenu() {
  return (
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
    </SidebarMenu>
  );
}

export function SidebarCaptureActions({
  recordingState,
  onToggleRecording,
  onTakeScreenshot,
}: SidebarHeaderActionsProps) {
  const [screenshotFlashKey, setScreenshotFlashKey] = useState(0);
  const [isScreenshotting, setIsScreenshotting] = useState(false);
  const isRecording = recordingState === "recording";
  const isRecordingBusy = recordingState === "starting" || recordingState === "stopping";
  const recordLabel =
    recordingState === "starting"
      ? "Starting recording"
      : recordingState === "stopping"
        ? "Stopping recording"
        : isRecording
          ? "Stop recording"
          : "Record";

  const handleScreenshot = async () => {
    if (isScreenshotting) {
      return;
    }

    setIsScreenshotting(true);

    try {
      await onTakeScreenshot();
      setScreenshotFlashKey((key) => key + 1);
    } catch (error) {
      console.error("Could not save screenshot.", error);
    } finally {
      setIsScreenshotting(false);
    }
  };

  return (
    <SidebarMenu className="flex-row items-center gap-1">
      <SidebarMenuItem className="shrink-0">
        <Button
          type="button"
          variant="outline"
          size="icon"
          aria-label={recordLabel}
          aria-pressed={isRecording}
          title={recordLabel}
          disabled={isRecordingBusy}
          data-recording={isRecording || recordingState === "starting"}
          onClick={onToggleRecording}
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
          aria-label={isScreenshotting ? "Saving screenshot" : "Screenshot"}
          title={isScreenshotting ? "Saving screenshot" : "Screenshot"}
          disabled={isScreenshotting}
          onClick={handleScreenshot}
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
  );
}

export function SidebarHeaderActions(props: SidebarHeaderActionsProps) {
  return (
    <div className="flex items-center gap-1">
      <SidebarUserMenu />
      <SidebarCaptureActions {...props} />
    </div>
  );
}

export function SidebarRuntimeActions({
  recordingState,
  onToggleRecording,
  onTakeScreenshot,
  autoOrbit,
  onAutoOrbitChange,
  renderEnabled,
  onRenderEnabledChange,
}: SidebarRuntimeActionsProps) {
  return (
    <div className="flex w-full items-center justify-between gap-2">
      <SidebarUserMenu />
      <div className="flex items-center gap-1">
        <SidebarCaptureActions
          recordingState={recordingState}
          onToggleRecording={onToggleRecording}
          onTakeScreenshot={onTakeScreenshot}
        />
        <Button
          type="button"
          variant="outline"
          size="icon"
          aria-label={renderEnabled ? "Pause rendering" : "Resume rendering"}
          aria-pressed={renderEnabled}
          title={renderEnabled ? "Pause rendering" : "Resume rendering"}
          data-playing={renderEnabled}
          onClick={() => onRenderEnabledChange(!renderEnabled)}
          className={cn(
            "render-play-button relative overflow-hidden rounded-md border-sidebar-border bg-sidebar text-sidebar-foreground/70 shadow-none hover:bg-sidebar-accent hover:text-sidebar-accent-foreground",
            renderEnabled &&
              "border-success/40 bg-success/10 text-success hover:bg-success/15 hover:text-success",
          )}
        >
          {renderEnabled ? <PauseIcon className="size-3.5" /> : <PlayIcon className="size-3.5" />}
        </Button>
        <Button
          type="button"
          variant="outline"
          size="icon"
          aria-label={autoOrbit ? "Stop auto orbit" : "Start auto orbit"}
          aria-pressed={autoOrbit}
          title={autoOrbit ? "Stop auto orbit" : "Start auto orbit"}
          onClick={() => onAutoOrbitChange(!autoOrbit)}
          className={cn(
            "rounded-md border-sidebar-border bg-sidebar text-sidebar-foreground/70 shadow-none hover:bg-sidebar-accent hover:text-sidebar-accent-foreground",
            autoOrbit &&
              "border-sidebar-border bg-sidebar-accent text-sidebar-accent-foreground shadow-[inset_0_0_0_1px_var(--sidebar-border)]",
          )}
        >
          <RotateCwIcon className={cn("size-3.5", autoOrbit && "text-sky-500")} />
        </Button>
      </div>
    </div>
  );
}
