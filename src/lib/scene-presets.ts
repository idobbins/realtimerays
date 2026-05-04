import type { ComponentType } from "react";
import {
  BoxesIcon,
  CircleDotIcon,
  Layers3Icon,
  OrbitIcon,
  SparklesIcon,
} from "lucide-react";

import type { RenderSphere, SphereMaterial } from "@/lib/render-settings";

type ScenePresetObject = {
  id: string;
  name: string;
  center: [number, number, number];
  radius: number;
  role?: "auto" | "light";
};

export type ScenePreset = {
  id: string;
  label: string;
  shortLabel: string;
  description: string;
  icon: ComponentType<{ className?: string }>;
  objects: ScenePresetObject[];
};

export const defaultScenePresetId = "showcase";
export const defaultSceneMaterialSeed = 12;

export const scenePresets = [
  {
    id: "showcase",
    label: "Showcase",
    shortLabel: "Core",
    description: "balanced hero trio",
    icon: BoxesIcon,
    objects: [
      { id: "center", name: "Center sphere", center: [0, 0, 0], radius: 1 },
      { id: "left", name: "Left sphere", center: [-2.2, 0, -0.5], radius: 1 },
      { id: "right", name: "Right sphere", center: [2.2, 0, -0.5], radius: 1 },
      { id: "front-a", name: "Front small", center: [0.6, -0.6, 1.6], radius: 0.4 },
      { id: "front-b", name: "Front bead", center: [-0.8, -0.7, 1.4], radius: 0.3 },
      {
        id: "key-light",
        name: "Key light",
        center: [0, 3.5, -1],
        radius: 1.2,
        role: "light",
      },
    ],
  },
  {
    id: "orbit",
    label: "Orbit",
    shortLabel: "Ring",
    description: "radial material study",
    icon: OrbitIcon,
    objects: [
      { id: "core", name: "Core sphere", center: [0, 0, 0], radius: 0.95 },
      { id: "orb-01", name: "Orbit 01", center: [2.35, -0.35, 0], radius: 0.45 },
      { id: "orb-02", name: "Orbit 02", center: [1.66, -0.35, 1.66], radius: 0.45 },
      { id: "orb-03", name: "Orbit 03", center: [0, -0.35, 2.35], radius: 0.45 },
      { id: "orb-04", name: "Orbit 04", center: [-1.66, -0.35, 1.66], radius: 0.45 },
      { id: "orb-05", name: "Orbit 05", center: [-2.35, -0.35, 0], radius: 0.45 },
      { id: "orb-06", name: "Orbit 06", center: [-1.66, -0.35, -1.66], radius: 0.45 },
      { id: "orb-07", name: "Orbit 07", center: [0, -0.35, -2.35], radius: 0.45 },
      { id: "orb-08", name: "Orbit 08", center: [1.66, -0.35, -1.66], radius: 0.45 },
      {
        id: "top-light",
        name: "Top light",
        center: [0, 3.4, -0.9],
        radius: 1.1,
        role: "light",
      },
    ],
  },
  {
    id: "steps",
    label: "Steps",
    shortLabel: "Stack",
    description: "rising depth staircase",
    icon: Layers3Icon,
    objects: [
      { id: "step-01", name: "Step 01", center: [-2.5, -0.45, 1.4], radius: 0.55 },
      { id: "step-02", name: "Step 02", center: [-1.55, -0.25, 0.85], radius: 0.65 },
      { id: "step-03", name: "Step 03", center: [-0.55, -0.05, 0.25], radius: 0.78 },
      { id: "step-04", name: "Step 04", center: [0.55, 0.18, -0.45], radius: 0.9 },
      { id: "step-05", name: "Step 05", center: [1.7, 0.42, -1.2], radius: 1.05 },
      { id: "anchor", name: "Anchor", center: [2.6, -0.45, 1.05], radius: 0.5 },
      {
        id: "side-light",
        name: "Side light",
        center: [-2.4, 3.1, -1.4],
        radius: 1,
        role: "light",
      },
    ],
  },
  {
    id: "scatter",
    label: "Scatter",
    shortLabel: "Field",
    description: "compact random field",
    icon: CircleDotIcon,
    objects: [
      { id: "node-01", name: "Node 01", center: [-2.7, -0.5, -1.1], radius: 0.5 },
      { id: "node-02", name: "Node 02", center: [-1.7, -0.2, 0.9], radius: 0.8 },
      { id: "node-03", name: "Node 03", center: [-0.8, -0.55, -1.8], radius: 0.45 },
      { id: "node-04", name: "Node 04", center: [0, -0.05, 0], radius: 0.95 },
      { id: "node-05", name: "Node 05", center: [0.95, -0.48, 1.65], radius: 0.52 },
      { id: "node-06", name: "Node 06", center: [1.65, -0.25, -0.85], radius: 0.75 },
      { id: "node-07", name: "Node 07", center: [2.55, -0.6, 0.7], radius: 0.4 },
      { id: "node-08", name: "Node 08", center: [2.75, -0.35, -1.9], radius: 0.62 },
      {
        id: "field-light",
        name: "Field light",
        center: [0.4, 3.2, -1.5],
        radius: 1.15,
        role: "light",
      },
    ],
  },
  {
    id: "glints",
    label: "Glints",
    shortLabel: "Spec",
    description: "small reflective lineup",
    icon: SparklesIcon,
    objects: [
      { id: "glint-01", name: "Glint 01", center: [-2.7, -0.35, 0.35], radius: 0.65 },
      { id: "glint-02", name: "Glint 02", center: [-1.55, -0.48, 1.35], radius: 0.52 },
      { id: "glint-03", name: "Glint 03", center: [-0.45, -0.15, -0.45], radius: 0.85 },
      { id: "glint-04", name: "Glint 04", center: [0.8, -0.52, 1.05], radius: 0.48 },
      { id: "glint-05", name: "Glint 05", center: [1.75, -0.25, -0.6], radius: 0.72 },
      { id: "glint-06", name: "Glint 06", center: [2.65, -0.55, 0.65], radius: 0.42 },
      {
        id: "strip-light",
        name: "Strip light",
        center: [0, 2.9, -1.8],
        radius: 0.95,
        role: "light",
      },
    ],
  },
] as const satisfies readonly ScenePreset[];

export type ScenePresetId = (typeof scenePresets)[number]["id"];

const materialLooks: Array<Pick<RenderSphere, "material" | "albedo" | "emission" | "fuzz">> = [
  { material: "diffuse", albedo: [0.9, 0.28, 0.24], emission: [0, 0, 0], fuzz: 0 },
  { material: "diffuse", albedo: [0.22, 0.72, 0.42], emission: [0, 0, 0], fuzz: 0 },
  { material: "diffuse", albedo: [0.34, 0.46, 0.96], emission: [0, 0, 0], fuzz: 0 },
  { material: "diffuse", albedo: [0.9, 0.78, 0.34], emission: [0, 0, 0], fuzz: 0 },
  { material: "metal", albedo: [0.95, 0.93, 0.88], emission: [0, 0, 0], fuzz: 0.03 },
  { material: "metal", albedo: [0.95, 0.68, 0.24], emission: [0, 0, 0], fuzz: 0.18 },
  { material: "metal", albedo: [0.62, 0.72, 0.82], emission: [0, 0, 0], fuzz: 0.28 },
  { material: "glass", albedo: [0.94, 0.97, 1], emission: [0, 0, 0], fuzz: 0 },
  { material: "glass", albedo: [0.76, 0.93, 0.9], emission: [0, 0, 0], fuzz: 0 },
];

const lightLook: Pick<RenderSphere, "material" | "albedo" | "emission" | "fuzz"> = {
  material: "light",
  albedo: [1, 1, 1],
  emission: [6, 5, 4],
  fuzz: 0,
};

function seededValue(seed: number, index: number) {
  let value = (seed + 1) * 0x9e3779b1 + index * 0x85ebca6b;
  value ^= value >>> 16;
  value = Math.imul(value, 0x7feb352d);
  value ^= value >>> 15;
  value = Math.imul(value, 0x846ca68b);
  value ^= value >>> 16;

  return value >>> 0;
}

function objectMaterial(seed: number, index: number) {
  return materialLooks[seededValue(seed, index) % materialLooks.length];
}

export function getScenePreset(id: string): ScenePreset {
  return scenePresets.find((preset) => preset.id === id) ?? scenePresets[0];
}

export function createSceneSpheres(presetId: string, materialSeed: number): RenderSphere[] {
  const preset = getScenePreset(presetId);

  return preset.objects.map((object, index) => {
    const material = object.role === "light" ? lightLook : objectMaterial(materialSeed, index);

    return {
      id: `${preset.id}-${object.id}`,
      name: object.name,
      center: object.center,
      radius: object.radius,
      material: material.material as SphereMaterial,
      albedo: material.albedo,
      emission: material.emission,
      fuzz: material.fuzz,
    };
  });
}
