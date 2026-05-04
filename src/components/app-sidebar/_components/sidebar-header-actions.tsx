"use client";

import { useState } from "react";
import {
  CameraIcon,
  ChevronDownIcon,
  CircleDotIcon,
  Layers3Icon,
  LogOutIcon,
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

export function SidebarHeaderActions() {
  const [isRecording, setIsRecording] = useState(false);
  const [screenshotFlashKey, setScreenshotFlashKey] = useState(0);

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
  );
}
