"use client";

import type { ComponentType } from "react";
import { ApertureIcon, FocusIcon, ScanIcon, VideoIcon } from "lucide-react";

import { AccordionContent, AccordionItem } from "@/components/ui/accordion";
import { Button } from "@/components/ui/button";
import { ButtonGroup } from "@/components/ui/button-group";
import { Input } from "@/components/ui/input";
import type { CameraType, RenderSettings } from "@/lib/render-settings";
import { cn } from "@/lib/utils";

import { SettingRow } from "./setting-row";
import {
  SidebarSectionMeta,
  SidebarSectionTrigger,
  type RenderSettingChange,
} from "./sidebar-section";

const cameraModes: Array<{
  value: CameraType;
  label: string;
  shortLabel: string;
  description: string;
}> = [
  {
    value: "perspective",
    label: "Perspective",
    shortLabel: "Persp",
    description: "single-origin projection",
  },
  {
    value: "orthographic",
    label: "Orthographic",
    shortLabel: "Ortho",
    description: "parallel inspection rays",
  },
  {
    value: "thin-lens",
    label: "Thin lens",
    shortLabel: "Lens",
    description: "aperture and focal plane",
  },
];

const cameraModeLabels: Record<CameraType, string> = {
  perspective: "Perspective",
  orthographic: "Orthographic",
  "thin-lens": "Thin Lens",
};

function clampNumber(value: number, min: number, max: number) {
  return Math.max(min, Math.min(max, value));
}

function CameraModeSetting({
  value,
  onChange,
}: {
  value: CameraType;
  onChange: (value: CameraType) => void;
}) {
  return (
    <div className="grid gap-1.5">
      <span className="px-0.5 text-[11px] font-medium text-muted-foreground">Camera model</span>
      <ButtonGroup className="grid w-full grid-cols-3">
        {cameraModes.map((mode) => {
          const selected = value === mode.value;

          return (
            <Button
              key={mode.value}
              type="button"
              variant="outline"
              size="sm"
              aria-pressed={selected}
              title={`${mode.label}: ${mode.description}`}
              onClick={() => onChange(mode.value)}
              className={cn(
                "h-8 min-w-0 bg-background/60 px-2 text-[11px] text-muted-foreground shadow-none",
                selected &&
                  "border-sidebar-border bg-sidebar-accent text-sidebar-accent-foreground shadow-[inset_0_0_0_1px_var(--sidebar-border)]",
              )}
            >
              <span className="truncate">{mode.shortLabel}</span>
            </Button>
          );
        })}
      </ButtonGroup>
      <div className="flex min-h-5 items-center gap-1.5 px-0.5 text-[11px] text-muted-foreground">
        <VideoIcon className="size-3" />
        <span>{cameraModes.find((mode) => mode.value === value)?.description}</span>
      </div>
    </div>
  );
}

function NumberSetting({
  label,
  value,
  min,
  max,
  step,
  suffix,
  icon: Icon,
  onChange,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  suffix: string;
  icon?: ComponentType<{ className?: string }>;
  onChange: (value: number) => void;
}) {
  return (
    <SettingRow label={label}>
      <span className="flex shrink-0 items-center gap-1">
        {Icon ? <Icon className="size-3 text-muted-foreground" /> : null}
        <Input
          type="number"
          min={min}
          max={max}
          step={step}
          value={value}
          onChange={(event) => {
            onChange(clampNumber(Number(event.currentTarget.value) || min, min, max));
          }}
          className="h-7 w-20 bg-background/60 px-2 text-right text-[11px]"
          aria-label={label}
        />
        <span className="w-7 text-[10px] text-muted-foreground">{suffix}</span>
      </span>
    </SettingRow>
  );
}

function PerspectiveCameraSettings({
  settings,
  onSettingChange,
}: {
  settings: RenderSettings;
  onSettingChange: RenderSettingChange<RenderSettings>;
}) {
  return (
    <>
      <NumberSetting
        label="vertical field of view"
        value={settings.fovDegrees}
        min={20}
        max={120}
        step={1}
        suffix="deg"
        icon={ApertureIcon}
        onChange={(value) => onSettingChange("fovDegrees", value)}
      />
    </>
  );
}

function OrthographicCameraSettings({
  settings,
  onSettingChange,
}: {
  settings: RenderSettings;
  onSettingChange: RenderSettingChange<RenderSettings>;
}) {
  return (
    <>
      <NumberSetting
        label="orthographic scale"
        value={settings.orthoScale}
        min={0.5}
        max={12}
        step={0.1}
        suffix="u"
        icon={ScanIcon}
        onChange={(value) => onSettingChange("orthoScale", value)}
      />
    </>
  );
}

function ThinLensCameraSettings({
  settings,
  onSettingChange,
}: {
  settings: RenderSettings;
  onSettingChange: RenderSettingChange<RenderSettings>;
}) {
  return (
    <>
      <NumberSetting
        label="vertical field of view"
        value={settings.fovDegrees}
        min={20}
        max={120}
        step={1}
        suffix="deg"
        icon={ApertureIcon}
        onChange={(value) => onSettingChange("fovDegrees", value)}
      />
      <NumberSetting
        label="focus distance"
        value={settings.focusDistance}
        min={0.5}
        max={30}
        step={0.1}
        suffix="u"
        icon={FocusIcon}
        onChange={(value) => onSettingChange("focusDistance", value)}
      />
      <NumberSetting
        label="aperture radius"
        value={settings.apertureRadius}
        min={0}
        max={0.5}
        step={0.01}
        suffix="u"
        icon={ApertureIcon}
        onChange={(value) => onSettingChange("apertureRadius", value)}
      />
    </>
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
      <SidebarSectionTrigger
        icon={ApertureIcon}
        title="Camera"
        value={cameraModeLabels[settings.cameraType]}
      />
      <AccordionContent className="space-y-3 px-2 pb-3">
        <div className="grid gap-2">
          <SidebarSectionMeta label="Projection" value="ray generator" />
          <CameraModeSetting
            value={settings.cameraType}
            onChange={(value) => onSettingChange("cameraType", value)}
          />
          {settings.cameraType === "perspective" ? (
            <PerspectiveCameraSettings settings={settings} onSettingChange={onSettingChange} />
          ) : null}
          {settings.cameraType === "orthographic" ? (
            <OrthographicCameraSettings settings={settings} onSettingChange={onSettingChange} />
          ) : null}
          {settings.cameraType === "thin-lens" ? (
            <ThinLensCameraSettings settings={settings} onSettingChange={onSettingChange} />
          ) : null}
        </div>
      </AccordionContent>
    </AccordionItem>
  );
}
