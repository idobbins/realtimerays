"use client";

import {
  BoxesIcon,
  DicesIcon,
  HashIcon,
  RefreshCcwIcon,
  ShuffleIcon,
} from "lucide-react";

import { AccordionContent, AccordionItem } from "@/components/ui/accordion";
import { Button } from "@/components/ui/button";
import { ButtonGroup, ButtonGroupText } from "@/components/ui/button-group";
import { Input } from "@/components/ui/input";
import {
  createSceneSpheres,
  defaultSceneMaterialSeed,
  defaultScenePresetId,
  getScenePreset,
  scenePresets,
  type ScenePresetId,
} from "@/lib/scene-presets";
import type { RenderSettings, RenderSphere } from "@/lib/render-settings";
import { cn } from "@/lib/utils";

import {
  SidebarSectionMeta,
  SidebarSectionTrigger,
  type RenderSettingChange,
} from "./sidebar-section";

function clampSeed(value: number) {
  return Math.max(0, Math.min(999, Math.round(value)));
}

function swatchColor(value: [number, number, number], scale = 1) {
  const [r, g, b] = value.map((channel) =>
    Math.max(0, Math.min(255, Math.round((channel / scale) * 255))),
  );
  return `rgb(${r} ${g} ${b})`;
}

function sceneBounds(spheres: RenderSphere[]) {
  return spheres.reduce(
    (bounds, sphere) => {
      bounds.minX = Math.min(bounds.minX, sphere.center[0] - sphere.radius);
      bounds.maxX = Math.max(bounds.maxX, sphere.center[0] + sphere.radius);
      bounds.minZ = Math.min(bounds.minZ, sphere.center[2] - sphere.radius);
      bounds.maxZ = Math.max(bounds.maxZ, sphere.center[2] + sphere.radius);
      return bounds;
    },
    { minX: Infinity, maxX: -Infinity, minZ: Infinity, maxZ: -Infinity },
  );
}

function sphereDotStyle(sphere: RenderSphere, spheres: RenderSphere[]) {
  const bounds = sceneBounds(spheres);
  const width = Math.max(1, bounds.maxX - bounds.minX);
  const depth = Math.max(1, bounds.maxZ - bounds.minZ);
  const largestSpan = Math.max(width, depth);
  const size = Math.max(6, Math.min(18, (sphere.radius / largestSpan) * 42));

  return {
    left: `${((sphere.center[0] - bounds.minX) / width) * 76 + 12}%`,
    top: `${((sphere.center[2] - bounds.minZ) / depth) * 68 + 16}%`,
    width: `${size}px`,
    height: `${size}px`,
    backgroundColor:
      sphere.material === "light" ? swatchColor(sphere.emission, 12) : swatchColor(sphere.albedo),
  };
}

function SceneMiniMap({
  spheres,
  className,
}: {
  spheres: RenderSphere[];
  className?: string;
}) {
  return (
    <div
      aria-hidden="true"
      className={cn("relative h-12 overflow-hidden rounded-md bg-background/45", className)}
    >
      {spheres.map((sphere) => (
        <span
          key={sphere.id}
          className={cn(
            "absolute -translate-x-1/2 -translate-y-1/2 rounded-full border border-sidebar-border shadow-[inset_0_0_0_1px_rgba(255,255,255,0.3)]",
            sphere.material === "light" && "shadow-[0_0_14px_rgba(251,191,36,0.7)]",
          )}
          style={sphereDotStyle(sphere, spheres)}
        />
      ))}
    </div>
  );
}

function PresetButton({
  presetId,
  seed,
  selected,
  onSelect,
}: {
  presetId: ScenePresetId;
  seed: number;
  selected: boolean;
  onSelect: (presetId: ScenePresetId) => void;
}) {
  const preset = getScenePreset(presetId);
  const Icon = preset.icon;
  const previewSpheres = createSceneSpheres(preset.id, seed);

  return (
    <button
      type="button"
      aria-pressed={selected}
      onClick={() => onSelect(presetId)}
      className={cn(
        "grid min-h-14 grid-cols-[3.25rem_1fr_auto] items-center gap-2 rounded-md border border-transparent px-2 py-1.5 text-left text-sidebar-foreground/80 transition-colors hover:bg-sidebar-accent/50 focus-visible:ring-[3px] focus-visible:ring-ring/50 focus-visible:outline-1 focus-visible:outline-ring",
        selected &&
          "border-sky-400/50 bg-sidebar-accent/70 text-sidebar-accent-foreground shadow-[0_1px_8px_rgba(56,189,248,0.14)]",
      )}
    >
      <SceneMiniMap
        spheres={previewSpheres}
        className="h-10 rounded-sm bg-muted/35 dark:bg-background/35"
      />
      <span className="grid min-w-0 gap-0.5">
        <span className="flex min-w-0 items-center gap-1.5 text-[12px] font-medium">
          <Icon className="size-3.5 shrink-0 text-muted-foreground" />
          <span className="truncate">{preset.label}</span>
        </span>
        <span className="truncate text-[10px] text-muted-foreground">{preset.description}</span>
      </span>
      <span className="rounded-sm bg-background/55 px-1.5 py-0.5 text-[10px] text-muted-foreground">
        {previewSpheres.length}
      </span>
    </button>
  );
}

function SeedControl({
  seed,
  onChange,
}: {
  seed: number;
  onChange: (seed: number) => void;
}) {
  return (
    <div className="grid gap-1.5">
      <div className="flex items-center justify-between gap-2 px-0.5">
        <span className="text-[11px] font-medium text-muted-foreground">Material seed</span>
        <span className="flex items-center gap-1 text-[10px] text-muted-foreground">
          <DicesIcon className="size-3" />
          deterministic
        </span>
      </div>
      <ButtonGroup className="w-full">
        <ButtonGroupText className="h-8 bg-background/60 px-2 text-muted-foreground">
          <HashIcon className="size-3" />
        </ButtonGroupText>
        <Input
          type="number"
          value={seed}
          min={0}
          max={999}
          step={1}
          onChange={(event) => onChange(clampSeed(Number(event.currentTarget.value) || 0))}
          className="h-8 bg-background/60 px-2 text-right text-[11px]"
          aria-label="Material seed"
        />
        <Button
          type="button"
          variant="outline"
          size="icon"
          title="Next material seed"
          aria-label="Next material seed"
          onClick={() => onChange(clampSeed(seed + 1))}
          className="size-8 bg-background/60 shadow-none"
        >
          <ShuffleIcon className="size-3.5" />
        </Button>
        <Button
          type="button"
          variant="outline"
          size="icon"
          title="Reset material seed"
          aria-label="Reset material seed"
          onClick={() => onChange(defaultSceneMaterialSeed)}
          className="size-8 bg-background/60 shadow-none"
        >
          <RefreshCcwIcon className="size-3.5" />
        </Button>
      </ButtonGroup>
    </div>
  );
}

export function SceneSettings({
  settings,
  onSettingChange,
}: {
  settings: RenderSettings;
  onSettingChange: RenderSettingChange<RenderSettings>;
}) {
  const presetId = settings.scenePresetId ?? defaultScenePresetId;
  const materialSeed = settings.sceneMaterialSeed ?? defaultSceneMaterialSeed;
  const preset = getScenePreset(presetId);

  const applyScene = (nextPresetId: ScenePresetId, nextSeed: number) => {
    onSettingChange("scenePresetId", nextPresetId);
    onSettingChange("sceneMaterialSeed", nextSeed);
    onSettingChange("sceneSpheres", createSceneSpheres(nextPresetId, nextSeed));
  };

  return (
    <AccordionItem value="scene" className="border-b border-sidebar-border/70">
      <SidebarSectionTrigger icon={BoxesIcon} title="Scene" value={preset.shortLabel} />
      <AccordionContent className="space-y-3 px-2 pb-3">
        <div className="grid gap-2">
          <SidebarSectionMeta label="Arrangement" value="preset buffer" />
          <div className="grid gap-1">
            {scenePresets.map((scenePreset) => (
              <PresetButton
                key={scenePreset.id}
                presetId={scenePreset.id}
                seed={materialSeed}
                selected={presetId === scenePreset.id}
                onSelect={(nextPresetId) => applyScene(nextPresetId, materialSeed)}
              />
            ))}
          </div>
        </div>

        <SeedControl seed={materialSeed} onChange={(nextSeed) => applyScene(presetId, nextSeed)} />
      </AccordionContent>
    </AccordionItem>
  );
}
