"use client";

import { useEffect, useMemo, useState, type ComponentType } from "react";
import {
  BoxesIcon,
  CircleIcon,
  FlameIcon,
  GemIcon,
  Move3DIcon,
  PaletteIcon,
  RadiusIcon,
  SparklesIcon,
} from "lucide-react";

import { AccordionContent, AccordionItem } from "@/components/ui/accordion";
import { Button } from "@/components/ui/button";
import { ButtonGroup } from "@/components/ui/button-group";
import { Input } from "@/components/ui/input";
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectLabel,
  SelectTrigger,
} from "@/components/ui/select";
import type { RenderSettings, RenderSphere, SphereMaterial } from "@/lib/render-settings";
import { cn } from "@/lib/utils";

import {
  SidebarSectionMeta,
  SidebarSectionTrigger,
  type RenderSettingChange,
} from "./sidebar-section";

const materialOptions: Array<{
  value: SphereMaterial;
  label: string;
  icon: ComponentType<{ className?: string }>;
}> = [
  { value: "diffuse", label: "Diffuse", icon: CircleIcon },
  { value: "metal", label: "Metal", icon: SparklesIcon },
  { value: "glass", label: "Glass", icon: GemIcon },
  { value: "light", label: "Light", icon: FlameIcon },
];

const materialLabels: Record<SphereMaterial, string> = {
  diffuse: "Diffuse",
  metal: "Metal",
  glass: "Glass",
  light: "Light",
};

const albedoPresets: Array<{ label: string; value: [number, number, number] }> = [
  { label: "red", value: [0.9, 0.3, 0.3] },
  { label: "green", value: [0.2, 0.8, 0.4] },
  { label: "gold", value: [0.95, 0.7, 0.2] },
  { label: "white", value: [0.95, 0.95, 0.95] },
  { label: "glass", value: [0.95, 0.95, 1] },
];

const emissionPresets: Array<{ label: string; value: [number, number, number] }> = [
  { label: "warm", value: [6, 5, 4] },
  { label: "white", value: [6, 6, 6] },
  { label: "cool", value: [3, 5, 8] },
  { label: "red", value: [8, 3, 2] },
];

type SceneInspectorMode = "shape" | "material";

function clampNumber(value: number, min: number, max: number) {
  return Math.max(min, Math.min(max, value));
}

function numericInputValue(value: string, min: number, max: number) {
  return clampNumber(Number(value) || 0, min, max);
}

function swatchColor(value: [number, number, number], scale = 1) {
  const [r, g, b] = value.map((channel) =>
    clampNumber(Math.round((channel / scale) * 255), 0, 255),
  );
  return `rgb(${r} ${g} ${b})`;
}

function withTupleValue(
  tuple: [number, number, number],
  index: number,
  value: number,
): [number, number, number] {
  const next: [number, number, number] = [...tuple];
  next[index] = value;
  return next;
}

function materialDefaults(material: SphereMaterial): Partial<RenderSphere> {
  if (material === "light") {
    return { material, albedo: [1, 1, 1], emission: [6, 5, 4], fuzz: 0 };
  }

  if (material === "metal") {
    return { material, albedo: [0.95, 0.95, 0.95], emission: [0, 0, 0], fuzz: 0.12 };
  }

  if (material === "glass") {
    return { material, albedo: [0.95, 0.95, 1], emission: [0, 0, 0], fuzz: 0 };
  }

  return { material, albedo: [0.9, 0.3, 0.3], emission: [0, 0, 0], fuzz: 0 };
}

function MiniNumberInput({
  label,
  value,
  min,
  max,
  step,
  onChange,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  onChange: (value: number) => void;
}) {
  return (
    <label className="grid gap-1">
      <span className="text-[10px] font-medium text-muted-foreground">{label}</span>
      <Input
        type="number"
        value={value}
        min={min}
        max={max}
        step={step}
        onChange={(event) => onChange(numericInputValue(event.currentTarget.value, min, max))}
        className="h-7 bg-background/60 px-2 text-right text-[11px]"
        aria-label={label}
      />
    </label>
  );
}

function Vector3Editor({
  label,
  value,
  min,
  max,
  step,
  onChange,
}: {
  label: string;
  value: [number, number, number];
  min: number;
  max: number;
  step: number;
  onChange: (value: [number, number, number]) => void;
}) {
  return (
    <div className="grid gap-1.5 rounded-md px-2 py-1.5 text-sidebar-foreground/80 transition-colors hover:bg-sidebar-accent/60">
      <div className="flex items-center gap-1.5 text-[12px]">
        <Move3DIcon className="size-3 text-muted-foreground" />
        <span>{label}</span>
      </div>
      <div className="grid grid-cols-3 gap-1.5">
        {(["x", "y", "z"] as const).map((axis, index) => (
          <MiniNumberInput
            key={axis}
            label={axis.toUpperCase()}
            value={value[index]}
            min={min}
            max={max}
            step={step}
            onChange={(nextValue) => onChange(withTupleValue(value, index, nextValue))}
          />
        ))}
      </div>
    </div>
  );
}

function RadiusEditor({
  sphere,
  onChange,
}: {
  sphere: RenderSphere;
  onChange: (sphere: RenderSphere) => void;
}) {
  return (
    <div className="grid gap-1.5 rounded-md px-2 py-1.5 text-sidebar-foreground/80 transition-colors hover:bg-sidebar-accent/60">
      <div className="flex items-center gap-1.5 text-[12px]">
        <RadiusIcon className="size-3 text-muted-foreground" />
        <span>Radius</span>
      </div>
      <Input
        type="number"
        value={sphere.radius}
        min={0.05}
        max={3}
        step={0.05}
        onChange={(event) =>
          onChange({ ...sphere, radius: numericInputValue(event.currentTarget.value, 0.05, 3) })
        }
        className="h-7 bg-background/60 px-2 text-right text-[11px]"
        aria-label="Sphere radius"
      />
    </div>
  );
}

function MaterialSelector({
  value,
  onChange,
}: {
  value: SphereMaterial;
  onChange: (value: SphereMaterial) => void;
}) {
  const selectedMaterial =
    materialOptions.find((material) => material.value === value) ?? materialOptions[0];
  const SelectedIcon = selectedMaterial.icon;

  return (
    <div className="grid gap-1.5">
      <span className="px-0.5 text-[11px] font-medium text-muted-foreground">Material</span>
      <Select
        value={value}
        onValueChange={(nextValue) => {
          const nextMaterial = materialOptions.find((material) => material.value === nextValue);

          if (nextMaterial) {
            onChange(nextMaterial.value);
          }
        }}
      >
        <SelectTrigger size="sm" className="w-full bg-background/60 text-[11px]">
          <span className="flex min-w-0 flex-1 items-center gap-1.5">
            <SelectedIcon className="size-3" />
            <span className="truncate">{selectedMaterial.label}</span>
          </span>
        </SelectTrigger>
        <SelectContent align="start" alignItemWithTrigger={false} sideOffset={8}>
          <SelectGroup>
            <SelectLabel>Material</SelectLabel>
            {materialOptions.map((material) => {
              const Icon = material.icon;

              return (
                <SelectItem key={material.value} value={material.value} className="text-xs">
                  <Icon className="size-3" />
                  <span>{material.label}</span>
                </SelectItem>
              );
            })}
          </SelectGroup>
        </SelectContent>
      </Select>
    </div>
  );
}

function InspectorModeSwitch({
  value,
  onChange,
}: {
  value: SceneInspectorMode;
  onChange: (value: SceneInspectorMode) => void;
}) {
  const modes: Array<{
    value: SceneInspectorMode;
    label: string;
    icon: ComponentType<{ className?: string }>;
  }> = [
    { value: "shape", label: "Shape", icon: Move3DIcon },
    { value: "material", label: "Material", icon: PaletteIcon },
  ];

  return (
    <ButtonGroup className="grid w-full grid-cols-2">
      {modes.map((mode) => {
        const selected = value === mode.value;
        const Icon = mode.icon;

        return (
          <Button
            key={mode.value}
            type="button"
            variant="outline"
            size="sm"
            aria-pressed={selected}
            onClick={() => onChange(mode.value)}
            className={cn(
              "h-8 min-w-0 bg-background/60 px-2 text-[11px] text-muted-foreground shadow-none",
              selected &&
                "border-sidebar-border bg-sidebar-accent text-sidebar-accent-foreground shadow-[inset_0_0_0_1px_var(--sidebar-border)]",
            )}
          >
            <Icon className="size-3" />
            <span className="truncate">{mode.label}</span>
          </Button>
        );
      })}
    </ButtonGroup>
  );
}

function ColorPresetButtons({
  label,
  presets,
  scale,
  onSelect,
}: {
  label: string;
  presets: Array<{ label: string; value: [number, number, number] }>;
  scale: number;
  onSelect: (value: [number, number, number]) => void;
}) {
  return (
    <div className="flex items-center justify-between gap-2 px-0.5">
      <span className="text-[11px] font-medium text-muted-foreground">{label}</span>
      <div className="flex gap-1">
        {presets.map((preset) => (
          <button
            key={preset.label}
            type="button"
            title={preset.label}
            aria-label={preset.label}
            onClick={() => onSelect(preset.value)}
            className="size-5 rounded-sm border border-sidebar-border shadow-[inset_0_0_0_1px_rgba(255,255,255,0.28)] transition-transform hover:scale-110 focus-visible:ring-[3px] focus-visible:ring-ring/50 focus-visible:outline-1 focus-visible:outline-ring"
            style={{ backgroundColor: swatchColor(preset.value, scale) }}
          />
        ))}
      </div>
    </div>
  );
}

function ColorChannelEditor({
  label,
  value,
  max,
  onChange,
}: {
  label: string;
  value: [number, number, number];
  max: number;
  onChange: (value: [number, number, number]) => void;
}) {
  return (
    <div className="grid gap-1.5 rounded-md px-2 py-1.5 text-sidebar-foreground/80 transition-colors hover:bg-sidebar-accent/60">
      <ColorPresetButtons
        label={label}
        presets={max === 1 ? albedoPresets : emissionPresets}
        scale={max}
        onSelect={onChange}
      />
      <div className="grid grid-cols-3 gap-1.5">
        {(["r", "g", "b"] as const).map((channel, index) => (
          <MiniNumberInput
            key={channel}
            label={channel.toUpperCase()}
            value={value[index]}
            min={0}
            max={max}
            step={max === 1 ? 0.05 : 0.25}
            onChange={(nextValue) => onChange(withTupleValue(value, index, nextValue))}
          />
        ))}
      </div>
    </div>
  );
}

function MaterialProperties({
  sphere,
  onChange,
}: {
  sphere: RenderSphere;
  onChange: (sphere: RenderSphere) => void;
}) {
  if (sphere.material === "light") {
    return (
      <ColorChannelEditor
        label="Emission"
        value={sphere.emission}
        max={12}
        onChange={(emission) => onChange({ ...sphere, emission })}
      />
    );
  }

  return (
    <>
      <ColorChannelEditor
        label={sphere.material === "glass" ? "Tint" : "Albedo"}
        value={sphere.albedo}
        max={1}
        onChange={(albedo) => onChange({ ...sphere, albedo })}
      />
      {sphere.material === "metal" ? (
        <div className="grid gap-1.5 rounded-md px-2 py-1.5 text-sidebar-foreground/80 transition-colors hover:bg-sidebar-accent/60">
          <span className="text-[12px]">Fuzz</span>
          <Input
            type="number"
            value={sphere.fuzz}
            min={0}
            max={1}
            step={0.01}
            onChange={(event) =>
              onChange({ ...sphere, fuzz: numericInputValue(event.currentTarget.value, 0, 1) })
            }
            className="h-7 bg-background/60 px-2 text-right text-[11px]"
            aria-label="Metal fuzz"
          />
        </div>
      ) : null}
    </>
  );
}

function ShapeInspector({
  sphere,
  onChange,
}: {
  sphere: RenderSphere;
  onChange: (sphere: RenderSphere) => void;
}) {
  return (
    <div className="grid gap-2">
      <div className="flex items-center justify-between gap-2 px-0.5">
        <span className="text-[11px] font-medium text-muted-foreground">Sphere geometry</span>
        <span className="text-[10px] text-muted-foreground">{sphere.radius.toFixed(2)}u</span>
      </div>
      <Vector3Editor
        label="Position"
        value={sphere.center}
        min={-6}
        max={6}
        step={0.1}
        onChange={(center) => onChange({ ...sphere, center })}
      />
      <RadiusEditor sphere={sphere} onChange={onChange} />
    </div>
  );
}

function MaterialInspector({
  sphere,
  onChange,
}: {
  sphere: RenderSphere;
  onChange: (sphere: RenderSphere) => void;
}) {
  return (
    <div className="grid gap-2">
      <div className="flex items-center justify-between gap-2 px-0.5">
        <span className="text-[11px] font-medium text-muted-foreground">Surface response</span>
        <span className="text-[10px] text-muted-foreground">{materialLabels[sphere.material]}</span>
      </div>
      <MaterialSelector
        value={sphere.material}
        onChange={(material) => onChange({ ...sphere, ...materialDefaults(material) })}
      />
      <MaterialProperties sphere={sphere} onChange={onChange} />
    </div>
  );
}

function SpherePicker({
  spheres,
  selectedId,
  onSelect,
}: {
  spheres: RenderSphere[];
  selectedId: string;
  onSelect: (id: string) => void;
}) {
  return (
    <div className="grid max-h-38 gap-1 overflow-y-auto pr-1">
      {spheres.map((sphere) => {
        const selected = selectedId === sphere.id;

        return (
          <button
            key={sphere.id}
            type="button"
            onClick={() => onSelect(sphere.id)}
            data-selected={selected}
            className="flex min-h-8 items-center gap-2 rounded-md px-2 py-1.5 text-left text-[12px] text-sidebar-foreground/80 transition-colors hover:bg-sidebar-accent/60 data-[selected=true]:bg-sidebar-accent data-[selected=true]:text-sidebar-accent-foreground"
          >
            <span
              aria-hidden="true"
              className="size-3 shrink-0 rounded-full border border-sidebar-border"
              style={{
                backgroundColor:
                  sphere.material === "light"
                    ? swatchColor(sphere.emission, 12)
                    : swatchColor(sphere.albedo),
              }}
            />
            <span className="min-w-0 flex-1 truncate">{sphere.name}</span>
            <span className="text-[10px] text-muted-foreground">
              {materialLabels[sphere.material]}
            </span>
          </button>
        );
      })}
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
  const [selectedId, setSelectedId] = useState(settings.sceneSpheres[0]?.id ?? "");
  const [inspectorMode, setInspectorMode] = useState<SceneInspectorMode>("shape");
  const selectedSphere = useMemo(
    () =>
      settings.sceneSpheres.find((sphere) => sphere.id === selectedId) ?? settings.sceneSpheres[0],
    [selectedId, settings.sceneSpheres],
  );

  useEffect(() => {
    if (!selectedSphere && settings.sceneSpheres[0]) {
      setSelectedId(settings.sceneSpheres[0].id);
    }
  }, [selectedSphere, settings.sceneSpheres]);

  const updateSphere = (nextSphere: RenderSphere) => {
    onSettingChange(
      "sceneSpheres",
      settings.sceneSpheres.map((sphere) => (sphere.id === nextSphere.id ? nextSphere : sphere)),
    );
  };

  return (
    <AccordionItem value="scene" className="border-b border-sidebar-border/70">
      <SidebarSectionTrigger
        icon={BoxesIcon}
        title="Scene"
        value={`${settings.sceneSpheres.length} spheres`}
      />
      <AccordionContent className="space-y-3 px-2 pb-3">
        <div className="grid gap-2">
          <SidebarSectionMeta label="Objects" value="editable buffer" />
          <SpherePicker
            spheres={settings.sceneSpheres}
            selectedId={selectedSphere?.id ?? ""}
            onSelect={setSelectedId}
          />
          {selectedSphere ? (
            <div className="grid gap-2 border-t border-sidebar-border/60 pt-2">
              <div className="flex items-center justify-between gap-2 px-0.5">
                <span className="min-w-0">
                  <span className="block truncate text-[12px] font-medium text-sidebar-foreground">
                    {selectedSphere.name}
                  </span>
                  <span className="block truncate text-[10px] text-muted-foreground">
                    Sphere · {materialLabels[selectedSphere.material]}
                  </span>
                </span>
                <span
                  aria-hidden="true"
                  className="size-5 shrink-0 rounded-sm border border-sidebar-border shadow-[inset_0_0_0_1px_rgba(255,255,255,0.28)]"
                  style={{
                    backgroundColor:
                      selectedSphere.material === "light"
                        ? swatchColor(selectedSphere.emission, 12)
                        : swatchColor(selectedSphere.albedo),
                  }}
                />
              </div>
              <InspectorModeSwitch value={inspectorMode} onChange={setInspectorMode} />
              {inspectorMode === "shape" ? (
                <ShapeInspector sphere={selectedSphere} onChange={updateSphere} />
              ) : (
                <MaterialInspector sphere={selectedSphere} onChange={updateSphere} />
              )}
            </div>
          ) : null}
        </div>
      </AccordionContent>
    </AccordionItem>
  );
}
