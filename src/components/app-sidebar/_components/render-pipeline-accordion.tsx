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
import type { RenderSettings } from "@/lib/render-settings";

import { CameraSettings } from "./camera-settings";
import { SamplingSettings } from "./sampling-settings";
import { SceneSettings } from "./scene-settings";
import { SettingRow } from "./setting-row";
import { SidebarSectionTrigger, type RenderSettingChange } from "./sidebar-section";

const pixelBudgetOptions = [
  { label: "0.4 MP", value: 400_000 },
  { label: "0.8 MP", value: 800_000 },
  { label: "1.6 MP", value: 1_600_000 },
  { label: "2.4 MP", value: 2_400_000 },
];

function RenderOutputSection({
  settings,
  onSettingChange,
}: {
  settings: RenderSettings;
  onSettingChange: RenderSettingChange<RenderSettings>;
}) {
  const activePixelBudget =
    pixelBudgetOptions.find((option) => option.value === settings.maxPixels)?.label ??
    `${Math.round(settings.maxPixels / 1000)}k px`;

  return (
    <AccordionItem value="render" className="border-b border-sidebar-border/70 last:border-b-0">
      <SidebarSectionTrigger icon={GaugeIcon} title="Render Output" value={activePixelBudget} />
      <AccordionContent className="space-y-3 px-2 pb-3">
        <div className="grid gap-2">
          <SettingRow label="render pixel budget">
            <Select
              value={String(settings.maxPixels)}
              onValueChange={(value) => onSettingChange("maxPixels", Number(value))}
            >
              <SelectTrigger size="sm" className="h-7 w-24 bg-background/60 text-[11px]">
                <SelectValue />
              </SelectTrigger>
              <SelectContent align="end" alignItemWithTrigger={false} sideOffset={8}>
                <SelectGroup>
                  <SelectLabel>Render pixel budget</SelectLabel>
                  {pixelBudgetOptions.map((option) => (
                    <SelectItem key={option.value} value={String(option.value)} className="text-xs">
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
