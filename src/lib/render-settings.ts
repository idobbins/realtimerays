import {
  createSceneSpheres,
  defaultSceneMaterialSeed,
  defaultScenePresetId,
  type ScenePresetId,
} from "@/lib/scene-presets";

export type CameraType = "perspective" | "orthographic" | "thin-lens";
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
  maxPixels: number;
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
  maxPixels: 1_600_000,
};
