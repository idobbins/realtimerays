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
  sceneSpheres: RenderSphere[];
  maxPixels: number;
};

export const defaultSceneSpheres: RenderSphere[] = [
  {
    id: "red-diffuse",
    name: "Red diffuse",
    center: [0, 0, 0],
    radius: 1,
    material: "diffuse",
    albedo: [0.9, 0.3, 0.3],
    emission: [0, 0, 0],
    fuzz: 0,
  },
  {
    id: "silver-metal",
    name: "Silver metal",
    center: [-2.2, 0, -0.5],
    radius: 1,
    material: "metal",
    albedo: [0.95, 0.95, 0.95],
    emission: [0, 0, 0],
    fuzz: 0.02,
  },
  {
    id: "glass-orb",
    name: "Glass orb",
    center: [2.2, 0, -0.5],
    radius: 1,
    material: "glass",
    albedo: [0.95, 0.95, 1],
    emission: [0, 0, 0],
    fuzz: 0,
  },
  {
    id: "green-diffuse",
    name: "Green diffuse",
    center: [0.6, -0.6, 1.6],
    radius: 0.4,
    material: "diffuse",
    albedo: [0.2, 0.8, 0.4],
    emission: [0, 0, 0],
    fuzz: 0,
  },
  {
    id: "gold-metal",
    name: "Gold metal",
    center: [-0.8, -0.7, 1.4],
    radius: 0.3,
    material: "metal",
    albedo: [0.95, 0.7, 0.2],
    emission: [0, 0, 0],
    fuzz: 0.3,
  },
  {
    id: "key-light",
    name: "Key light",
    center: [0, 3.5, -1],
    radius: 1.2,
    material: "light",
    albedo: [1, 1, 1],
    emission: [6, 5, 4],
    fuzz: 0,
  },
];

export const defaultRenderSettings: RenderSettings = {
  cameraType: "perspective",
  fovDegrees: 63,
  orthoScale: 3.5,
  apertureRadius: 0.05,
  focusDistance: 7.5,
  sceneSpheres: defaultSceneSpheres,
  maxPixels: 1_600_000,
};
