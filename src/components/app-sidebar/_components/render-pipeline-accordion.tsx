"use client";

import { ContrastIcon, GaugeIcon } from "lucide-react";

import { Accordion, AccordionContent, AccordionItem } from "@/components/ui/accordion";
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
  renderAspectRatioProfiles,
  toneMapOptions,
  type RenderAspectRatio,
  type RenderQuality,
  type RenderSettings,
  type ToneMap,
} from "@/lib/render-settings";

import { CameraSettings } from "./camera-settings";
import { SamplingSettings } from "./sampling-settings";
import { SceneSettings } from "./scene-settings";
import { SettingRow } from "./setting-row";
import { SidebarSectionTrigger, type RenderSettingChange } from "./sidebar-section";

const previewQualityOptions: Array<{
  label: string;
  value: RenderQuality;
  maxPixels: number;
  detail: string;
}> = [
  { label: "Draft", value: "draft", maxPixels: 400_000, detail: "fastest preview" },
  { label: "Balanced", value: "balanced", maxPixels: 1_600_000, detail: "default preview" },
  { label: "High", value: "high", maxPixels: 2_400_000, detail: "sharper preview" },
  {
    label: "Native",
    value: "native",
    maxPixels: Number.MAX_SAFE_INTEGER,
    detail: "full viewport",
  },
];

function RenderOutputSection({
  settings,
  onSettingChange,
}: {
  settings: RenderSettings;
  onSettingChange: RenderSettingChange<RenderSettings>;
}) {
  const activeQualityLabel =
    previewQualityOptions.find((option) => option.value === settings.renderQuality)?.label ??
    `${Math.round(settings.maxPixels / 1000)}k px`;
  const activeAspectRatio =
    renderAspectRatioProfiles.find((profile) => profile.value === settings.renderAspectRatio) ??
    renderAspectRatioProfiles[0];
  const sectionValue = `${activeQualityLabel} / ${activeAspectRatio.label}`;

  const setPreviewQuality = (quality: RenderQuality) => {
    const option = previewQualityOptions.find((candidate) => candidate.value === quality);

    if (!option) {
      return;
    }

    onSettingChange("renderQuality", option.value);
    onSettingChange("maxPixels", option.maxPixels);
  };

  return (
    <AccordionItem value="render" className="border-b border-sidebar-border/70 last:border-b-0">
      <SidebarSectionTrigger icon={GaugeIcon} title="Preview Quality" value={sectionValue} />
      <AccordionContent className="space-y-3 px-2 pb-3">
        <div className="grid gap-2">
          <SettingRow label="quality">
            <Select
              value={settings.renderQuality}
              onValueChange={(value) => setPreviewQuality(value as RenderQuality)}
            >
              <SelectTrigger size="sm" className="h-7 w-28 bg-background/60 text-[11px]">
                <SelectValue />
              </SelectTrigger>
              <SelectContent align="end" alignItemWithTrigger={false} sideOffset={8}>
                <SelectGroup>
                  <SelectLabel>Preview quality</SelectLabel>
                  {previewQualityOptions.map((option) => (
                    <SelectItem
                      key={option.value}
                      value={option.value}
                      title={option.detail}
                      className="text-xs"
                    >
                      {option.label}
                    </SelectItem>
                  ))}
                </SelectGroup>
              </SelectContent>
            </Select>
          </SettingRow>
          <SettingRow label="aspect ratio">
            <Select
              value={settings.renderAspectRatio}
              onValueChange={(value) =>
                onSettingChange("renderAspectRatio", value as RenderAspectRatio)
              }
            >
              <SelectTrigger size="sm" className="h-7 w-28 bg-background/60 text-[11px]">
                <SelectValue />
              </SelectTrigger>
              <SelectContent align="end" alignItemWithTrigger={false} sideOffset={8}>
                <SelectGroup>
                  <SelectLabel>Aspect ratio</SelectLabel>
                  {renderAspectRatioProfiles.map((profile) => (
                    <SelectItem key={profile.value} value={profile.value} className="text-xs">
                      {profile.label}
                    </SelectItem>
                  ))}
                </SelectGroup>
              </SelectContent>
            </Select>
          </SettingRow>
          <SettingRow label="tone map">
            <Select
              value={settings.toneMap}
              onValueChange={(value) => onSettingChange("toneMap", value as ToneMap)}
            >
              <SelectTrigger size="sm" className="h-7 w-28 bg-background/60 text-[11px]">
                <ContrastIcon className="size-3" />
                <SelectValue />
              </SelectTrigger>
              <SelectContent align="end" alignItemWithTrigger={false} sideOffset={8}>
                <SelectGroup>
                  <SelectLabel>Tone map</SelectLabel>
                  {toneMapOptions.map((option) => (
                    <SelectItem key={option.value} value={option.value} className="text-xs">
                      {option.label}
                    </SelectItem>
                  ))}
                </SelectGroup>
              </SelectContent>
            </Select>
          </SettingRow>
        </div>
      </AccordionContent>
    </AccordionItem>
  );
}

export function RenderPipelineAccordion({
  settings,
  onSettingChange,
}: {
  settings: RenderSettings;
  onSettingChange: RenderSettingChange<RenderSettings>;
}) {
  return (
    <Accordion multiple defaultValue={["scene", "camera", "render"]}>
      <SceneSettings settings={settings} onSettingChange={onSettingChange} />
      <CameraSettings settings={settings} onSettingChange={onSettingChange} />
      <SamplingSettings settings={settings} onSettingChange={onSettingChange} />
      <RenderOutputSection settings={settings} onSettingChange={onSettingChange} />
    </Accordion>
  );
}
