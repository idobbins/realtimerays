"use client";

import { SparklesIcon } from "lucide-react";

import { AccordionContent, AccordionItem } from "@/components/ui/accordion";
import { Button } from "@/components/ui/button";
import { ButtonGroup } from "@/components/ui/button-group";
import { Switch } from "@/components/ui/switch";
import type { RenderSettings } from "@/lib/render-settings";
import { cn } from "@/lib/utils";

import { SidebarSectionTrigger, type RenderSettingChange } from "./sidebar-section";

const sampleOptions = [1, 2, 4] as const;
const bounceOptions = [2, 4, 6, 8] as const;

function SamplingOptionButton({
  value,
  selected,
  label,
  onSelect,
}: {
  value: number;
  selected: boolean;
  label: string;
  onSelect: (value: number) => void;
}) {
  return (
    <Button
      type="button"
      variant="outline"
      size="sm"
      aria-pressed={selected}
      onClick={() => onSelect(value)}
      className={cn(
        "h-8 min-w-0 bg-background/60 px-2 text-[11px] text-muted-foreground shadow-none",
        selected &&
          "border-sky-400/50 bg-sidebar-accent/70 text-sidebar-accent-foreground shadow-[0_1px_8px_rgba(56,189,248,0.14)]",
      )}
    >
      <span className="truncate">{label}</span>
    </Button>
  );
}

function SamplingControl({
  label,
  value,
  options,
  format,
  onChange,
}: {
  label: string;
  value: number;
  options: readonly number[];
  format: (value: number) => string;
  onChange: (value: number) => void;
}) {
  return (
    <div className="grid gap-1.5">
      <div className="flex items-center justify-between gap-2 px-0.5">
        <span className="text-[11px] font-medium text-muted-foreground">{label}</span>
        <span className="text-[10px] text-muted-foreground">{format(value)}</span>
      </div>
      <ButtonGroup
        className="grid w-full"
        style={{ gridTemplateColumns: `repeat(${options.length}, 1fr)` }}
      >
        {options.map((option) => (
          <SamplingOptionButton
            key={option}
            value={option}
            label={format(option)}
            selected={value === option}
            onSelect={onChange}
          />
        ))}
      </ButtonGroup>
    </div>
  );
}

function TemporalAccumulationControl({
  enabled,
  onChange,
}: {
  enabled: boolean;
  onChange: (enabled: boolean) => void;
}) {
  const switchId = "temporal-accumulation";

  return (
    <label
      htmlFor={switchId}
      className="flex min-h-8 items-center justify-between gap-3 rounded-md px-2 py-1 text-[12px] text-sidebar-foreground/80 transition-colors hover:bg-sidebar-accent/50"
    >
      <span className="grid min-w-0 gap-0.5">
        <span className="truncate">Temporal accumulation</span>
        <span className="truncate text-[10px] text-muted-foreground">
          {enabled ? "progressive average" : "single dispatch"}
        </span>
      </span>
      <Switch
        id={switchId}
        size="sm"
        checked={enabled}
        onCheckedChange={onChange}
        aria-label="Temporal accumulation"
      />
    </label>
  );
}

export function SamplingSettings({
  settings,
  onSettingChange,
}: {
  settings: RenderSettings;
  onSettingChange: RenderSettingChange<RenderSettings>;
}) {
  const samplesPerDispatch = settings.samplesPerDispatch;
  const maxBounces = settings.maxBounces;
  const temporalAccumulation = settings.temporalAccumulation;

  return (
    <AccordionItem value="sampling" className="border-b border-sidebar-border/70">
      <SidebarSectionTrigger
        icon={SparklesIcon}
        title="Sampling"
        value={`${samplesPerDispatch} spp / ${maxBounces} bounces`}
      />
      <AccordionContent className="space-y-3 px-2 pb-3">
        <div className="grid gap-3">
          <TemporalAccumulationControl
            enabled={temporalAccumulation}
            onChange={(enabled) => onSettingChange("temporalAccumulation", enabled)}
          />
          <SamplingControl
            label="Samples per dispatch"
            value={samplesPerDispatch}
            options={sampleOptions}
            format={(value) => `${value} spp`}
            onChange={(value) => onSettingChange("samplesPerDispatch", value)}
          />
          <SamplingControl
            label="Max bounces"
            value={maxBounces}
            options={bounceOptions}
            format={(value) => `${value}`}
            onChange={(value) => onSettingChange("maxBounces", value)}
          />
        </div>
      </AccordionContent>
    </AccordionItem>
  );
}
