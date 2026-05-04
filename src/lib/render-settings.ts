export type CameraType = "perspective" | "orthographic" | "thin-lens";

export type RenderSettings = {
  cameraType: CameraType;
  fovDegrees: number;
  orthoScale: number;
  apertureRadius: number;
  focusDistance: number;
  maxPixels: number;
};

export const defaultRenderSettings: RenderSettings = {
  cameraType: "perspective",
  fovDegrees: 63,
  orthoScale: 3.5,
  apertureRadius: 0.05,
  focusDistance: 7.5,
  maxPixels: 1_600_000,
};
