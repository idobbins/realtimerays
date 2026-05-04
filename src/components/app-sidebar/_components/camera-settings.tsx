"use client";

import { ApertureIcon } from "lucide-react";

import { AccordionContent, AccordionItem } from "@/components/ui/accordion";
import { Input } from "@/components/ui/input";
import type { RenderSettings } from "@/lib/render-settings";

import { Readout, SettingRow } from "./setting-row";
import {
  SidebarSectionMeta,
  SidebarSectionTrigger,
  type RenderSettingChange,
} from "./sidebar-section";

function CameraModeSetting() {
  return (
    <SettingRow label="camera type">
      <Readout>pinhole</Readout>
    </SettingRow>
  );
}

function VerticalFovSetting({
  value,
  onChange,
}: {
  value: number;
  onChange: (value: number) => void;
}) {
  return (
    <SettingRow label="vertical field of view">
      <span className="flex shrink-0 items-center gap-1">
        <Input
          type="number"
          min={20}
          max={120}
          step={1}
          value={value}
          onChange={(event) => {
            onChange(Math.max(20, Math.min(120, Number(event.currentTarget.value) || 20)));
          }}
          className="h-7 w-20 bg-background/60 px-2 text-right text-[11px]"
          aria-label="Vertical field of view"
        />
        <span className="w-6 text-[10px] text-muted-foreground">deg</span>
      </span>
    </SettingRow>
  );
}

function CameraPoseSetting() {
  return (
    <SettingRow label="camera pose">
      <Readout>orbit viewport</Readout>
    </SettingRow>
  );
}

export function CameraSettings({
  settings,
  onSettingChange,
}: {
  settings: RenderSettings;
  onSettingChange: RenderSettingChange<RenderSettings>;
}) {
  return (
    <AccordionItem value="camera" className="border-b border-sidebar-border/70">
      <SidebarSectionTrigger icon={ApertureIcon} title="Camera" value="Pinhole" />
      <AccordionContent className="space-y-3 px-2 pb-3">
        <div className="grid gap-2">
          <SidebarSectionMeta label="Uniforms" value="cam + fov" />
          <CameraModeSetting />
          <VerticalFovSetting
            value={settings.fovDegrees}
            onChange={(value) => onSettingChange("fovDegrees", value)}
          />
          <CameraPoseSetting />
        </div>
      </AccordionContent>
    </AccordionItem>
  );
}
