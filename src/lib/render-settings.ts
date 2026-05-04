export type RenderSettings = {
  fovDegrees: number;
  maxPixels: number;
};

export const defaultRenderSettings: RenderSettings = {
  fovDegrees: 63,
  maxPixels: 1_600_000,
};
