import {
  createSceneSpheres,
  defaultSceneMaterialSeed,
  defaultScenePresetId,
  type ScenePresetId,
} from "@/lib/scene-presets";

export type CameraType = "perspective" | "orthographic" | "thin-lens";
export type RenderQuality = "draft" | "balanced" | "high" | "native";
export const toneMapOptions = [
  { value: "reinhard", label: "Reinhard" },
  { value: "aces", label: "ACES" },
  { value: "linear", label: "Linear" },
  { value: "none", label: "None" },
] as const;
export type ToneMap = (typeof toneMapOptions)[number]["value"];
export const renderAspectRatioProfiles = [
  { value: "fill", label: "Full", ratio: null },
  { value: "landscape", label: "16:9", ratio: 16 / 9 },
  { value: "square", label: "1:1", ratio: 1 },
  { value: "portrait", label: "4:5", ratio: 4 / 5 },
  { value: "vertical", label: "9:16", ratio: 9 / 16 },
] as const;
export type RenderAspectRatio = (typeof renderAspectRatioProfiles)[number]["value"];
export type SphereMaterial = "diffuse" | "metal" | "glass" | "light";

export type RenderSphere = {
  id: string;
  name: string;
  center: [number, number, number];
  radius: number;
  material: SphereMaterial;
  albedo: [number, number, number];
  emission: [number, number, number];
  fuzz: number;
};

export type RenderSettings = {
  cameraType: CameraType;
  fovDegrees: number;
  orthoScale: number;
  apertureRadius: number;
  focusDistance: number;
  scenePresetId: ScenePresetId;
  sceneMaterialSeed: number;
  sceneSpheres: RenderSphere[];
  samplesPerDispatch: number;
  maxBounces: number;
  temporalAccumulation: boolean;
  renderQuality: RenderQuality;
  renderAspectRatio: RenderAspectRatio;
  toneMap: ToneMap;
  maxPixels: number;
};

export type RenderViewport = {
  id: string;
  label: string;
  settings: RenderSettings;
  presentation?: "full" | "split-left" | "split-right";
};

export type RenderSession = {
  viewports: RenderViewport[];
};

export const defaultSceneSpheres: RenderSphere[] = createSceneSpheres(
  defaultScenePresetId,
  defaultSceneMaterialSeed,
);

export const defaultRenderSettings: RenderSettings = {
  cameraType: "perspective",
  fovDegrees: 63,
  orthoScale: 3.5,
  apertureRadius: 0.05,
  focusDistance: 7.5,
  scenePresetId: defaultScenePresetId,
  sceneMaterialSeed: defaultSceneMaterialSeed,
  sceneSpheres: defaultSceneSpheres,
  samplesPerDispatch: 1,
  maxBounces: 4,
  temporalAccumulation: true,
  renderQuality: "balanced",
  renderAspectRatio: "fill",
  toneMap: "reinhard",
  maxPixels: 1_600_000,
};
