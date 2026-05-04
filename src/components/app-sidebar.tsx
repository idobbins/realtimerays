"use client";

import { useState } from "react";
import {
  ApertureIcon,
  BoxesIcon,
  BugIcon,
  ChevronDownIcon,
  ContrastIcon,
  FolderOpenIcon,
  FrameIcon,
  GitBranchIcon,
  InfoIcon,
  Layers3Icon,
  LightbulbIcon,
  LogOutIcon,
  MonitorIcon,
  PaletteIcon,
  SparklesIcon,
  SunIcon,
  UserPlusIcon,
  WavesIcon,
  type LucideIcon,
} from "lucide-react";

import {
  Accordion,
  AccordionContent,
  AccordionItem,
  AccordionTrigger,
} from "@/components/ui/accordion";
import { Avatar, AvatarFallback } from "@/components/ui/avatar";
import { Badge } from "@/components/ui/badge";
import { Checkbox } from "@/components/ui/checkbox";
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuGroup,
  DropdownMenuItem,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu";
import { Input } from "@/components/ui/input";
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectLabel,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import {
  Sidebar,
  SidebarContent,
  SidebarGroupContent,
  SidebarHeader,
  SidebarMenu,
  SidebarMenuButton,
  SidebarMenuItem,
} from "@/components/ui/sidebar";
import { Switch } from "@/components/ui/switch";
import { Tooltip, TooltipContent, TooltipTrigger } from "@/components/ui/tooltip";

type OptionGroup = {
  title: string;
  options: string[];
};

type PipelineSection = {
  id: string;
  title: string;
  icon: LucideIcon;
  activeLabel: string;
  selectOptions: string[];
  groups: OptionGroup[];
  minimum: string[];
};

type CameraMode = {
  label: string;
  description: string;
  settingGroups: CameraSettingGroup[];
};

type CameraSettingGroup = {
  title: string;
  settings: CameraSetting[];
};

type CameraSetting =
  | {
      id: string;
      label: string;
      control: "number";
      defaultValue: number;
      min?: number;
      max?: number;
      step?: number;
      suffix?: string;
    }
  | {
      id: string;
      label: string;
      control: "select";
      defaultValue: string;
      options: string[];
    }
  | {
      id: string;
      label: string;
      control: "text";
      defaultValue: string;
    }
  | {
      id: string;
      label: string;
      control: "toggle";
      defaultValue: boolean;
    };

type CameraSettingValue = CameraSetting["defaultValue"];

const cameraModes: CameraMode[] = [
  {
    label: "Pinhole",
    description: "single origin perspective rays",
    settingGroups: [
      {
        title: "Projection",
        settings: [
          {
            id: "pinhole-fov",
            label: "vertical field of view",
            control: "number",
            defaultValue: 34,
            min: 1,
            max: 179,
            step: 1,
            suffix: "deg",
          },
          {
            id: "pinhole-aspect",
            label: "aspect ratio",
            control: "select",
            defaultValue: "16:9",
            options: ["1:1", "4:3", "16:9", "21:9"],
          },
          {
            id: "pinhole-near",
            label: "near clip",
            control: "number",
            defaultValue: 0.1,
            min: 0.01,
            step: 0.01,
          },
          {
            id: "pinhole-far",
            label: "far clip",
            control: "number",
            defaultValue: 100,
            min: 1,
            step: 1,
          },
        ],
      },
      {
        title: "Pose",
        settings: [
          { id: "pinhole-position", label: "position", control: "text", defaultValue: "3.1, 2.1, 5.7" },
          { id: "pinhole-target", label: "look target", control: "text", defaultValue: "0, 0.85, 0" },
          {
            id: "pinhole-up",
            label: "up vector",
            control: "select",
            defaultValue: "+Y",
            options: ["+Y", "+Z"],
          },
        ],
      },
    ],
  },
  {
    label: "Orthographic",
    description: "parallel rays with fixed scale",
    settingGroups: [
      {
        title: "Projection",
        settings: [
          {
            id: "ortho-scale",
            label: "orthographic scale",
            control: "number",
            defaultValue: 4,
            min: 0.1,
            step: 0.1,
          },
          {
            id: "ortho-aspect",
            label: "aspect ratio",
            control: "select",
            defaultValue: "16:9",
            options: ["1:1", "4:3", "16:9", "21:9"],
          },
          { id: "ortho-near", label: "near clip", control: "number", defaultValue: 0.1, min: 0.01, step: 0.01 },
          { id: "ortho-far", label: "far clip", control: "number", defaultValue: 100, min: 1, step: 1 },
        ],
      },
      {
        title: "Pose",
        settings: [
          { id: "ortho-position", label: "position", control: "text", defaultValue: "3.1, 2.1, 5.7" },
          {
            id: "ortho-direction",
            label: "look direction",
            control: "select",
            defaultValue: "-Z",
            options: ["-Z", "+Z", "-X", "+X", "-Y", "+Y"],
          },
          { id: "ortho-up", label: "up vector", control: "select", defaultValue: "+Y", options: ["+Y", "+Z"] },
        ],
      },
    ],
  },
  {
    label: "Thin lens",
    description: "perspective with depth of field",
    settingGroups: [
      {
        title: "Projection",
        settings: [
          { id: "thin-fov", label: "vertical field of view", control: "number", defaultValue: 34, min: 1, max: 179, step: 1, suffix: "deg" },
          { id: "thin-focal-length", label: "focal length", control: "number", defaultValue: 50, min: 1, step: 1, suffix: "mm" },
          {
            id: "thin-sensor-fit",
            label: "sensor fit",
            control: "select",
            defaultValue: "35mm full frame",
            options: ["35mm full frame", "APS-C", "Micro 4/3", "custom"],
          },
        ],
      },
      {
        title: "Lens",
        settings: [
          { id: "thin-aperture-radius", label: "aperture radius", control: "number", defaultValue: 0.04, min: 0, step: 0.01 },
          { id: "thin-focus-distance", label: "focus distance", control: "number", defaultValue: 5, min: 0.1, step: 0.1, suffix: "m" },
          { id: "thin-aperture-blades", label: "aperture blades", control: "number", defaultValue: 7, min: 3, max: 16, step: 1 },
          { id: "thin-bokeh-rotation", label: "bokeh rotation", control: "number", defaultValue: 0, min: 0, max: 360, step: 1, suffix: "deg" },
        ],
      },
    ],
  },
  {
    label: "Fisheye",
    description: "wide-angle curved projection",
    settingGroups: [
      {
        title: "Projection",
        settings: [
          {
            id: "fisheye-mapping",
            label: "fisheye mapping",
            control: "select",
            defaultValue: "equidistant",
            options: ["equidistant", "equisolid", "orthographic", "stereographic"],
          },
          { id: "fisheye-fov", label: "field of view", control: "number", defaultValue: 180, min: 1, max: 360, step: 1, suffix: "deg" },
          { id: "fisheye-crop", label: "image circle crop", control: "toggle", defaultValue: true },
        ],
      },
      {
        title: "Pose",
        settings: [
          { id: "fisheye-position", label: "position", control: "text", defaultValue: "0, 1.5, 0" },
          { id: "fisheye-direction", label: "look direction", control: "select", defaultValue: "-Z", options: ["-Z", "+Z", "-X", "+X"] },
          { id: "fisheye-roll", label: "roll", control: "number", defaultValue: 0, min: -180, max: 180, step: 1, suffix: "deg" },
        ],
      },
    ],
  },
  {
    label: "Equirectangular",
    description: "lat-long panorama rays",
    settingGroups: [
      {
        title: "Coverage",
        settings: [
          { id: "eq-horizontal", label: "horizontal coverage", control: "number", defaultValue: 360, min: 1, max: 360, step: 1, suffix: "deg" },
          { id: "eq-vertical", label: "vertical coverage", control: "number", defaultValue: 180, min: 1, max: 180, step: 1, suffix: "deg" },
          { id: "eq-seam", label: "seam longitude", control: "number", defaultValue: 0, min: -180, max: 180, step: 1, suffix: "deg" },
        ],
      },
      {
        title: "Pose",
        settings: [
          { id: "eq-position", label: "position", control: "text", defaultValue: "0, 1.5, 0" },
          { id: "eq-heading", label: "heading", control: "number", defaultValue: 0, min: -180, max: 180, step: 1, suffix: "deg" },
          { id: "eq-pitch", label: "pitch", control: "number", defaultValue: 0, min: -90, max: 90, step: 1, suffix: "deg" },
        ],
      },
    ],
  },
  {
    label: "Cubemap",
    description: "six square face cameras",
    settingGroups: [
      {
        title: "Faces",
        settings: [
          {
            id: "cube-resolution",
            label: "face resolution",
            control: "select",
            defaultValue: "1024",
            options: ["512", "1024", "2048", "4096"],
          },
          {
            id: "cube-face-order",
            label: "face order",
            control: "select",
            defaultValue: "+X -X +Y -Y +Z -Z",
            options: ["+X -X +Y -Y +Z -Z", "+X -X +Z -Z +Y -Y"],
          },
          { id: "cube-seam-fixup", label: "edge seam fixup", control: "toggle", defaultValue: true },
        ],
      },
      {
        title: "Pose",
        settings: [
          { id: "cube-position", label: "position", control: "text", defaultValue: "0, 1.5, 0" },
          {
            id: "cube-basis",
            label: "orientation basis",
            control: "select",
            defaultValue: "OpenGL",
            options: ["OpenGL", "DirectX", "custom"],
          },
        ],
      },
    ],
  },
  {
    label: "Stereo / VR",
    description: "paired eye projections",
    settingGroups: [
      {
        title: "Rig",
        settings: [
          { id: "stereo-eye-separation", label: "eye separation", control: "number", defaultValue: 0.064, min: 0, step: 0.001, suffix: "m" },
          { id: "stereo-convergence", label: "convergence distance", control: "number", defaultValue: 10, min: 0.1, step: 0.1, suffix: "m" },
          {
            id: "stereo-projection",
            label: "per-eye projection",
            control: "select",
            defaultValue: "asymmetric frustum",
            options: ["asymmetric frustum", "parallel", "toe-in"],
          },
        ],
      },
      {
        title: "Display",
        settings: [
          {
            id: "stereo-distortion",
            label: "lens distortion profile",
            control: "select",
            defaultValue: "none",
            options: ["none", "OpenXR", "custom"],
          },
          { id: "stereo-ipd-offset", label: "interpupillary offset", control: "number", defaultValue: 0, step: 0.001, suffix: "m" },
          {
            id: "stereo-layout",
            label: "view layout",
            control: "select",
            defaultValue: "side-by-side",
            options: ["side-by-side", "top-bottom", "per-eye targets"],
          },
        ],
      },
    ],
  },
  {
    label: "Oblique",
    description: "orthographic with projection shear",
    settingGroups: [
      {
        title: "Projection",
        settings: [
          { id: "oblique-scale", label: "orthographic scale", control: "number", defaultValue: 4, min: 0.1, step: 0.1 },
          { id: "oblique-shear", label: "shear angle", control: "number", defaultValue: 45, min: 0, max: 89, step: 1, suffix: "deg" },
          {
            id: "oblique-axis",
            label: "receding axis",
            control: "select",
            defaultValue: "+X",
            options: ["+X", "-X", "+Y", "-Y"],
          },
        ],
      },
      {
        title: "Clipping",
        settings: [
          { id: "oblique-near", label: "near clip", control: "number", defaultValue: 0.1, min: 0.01, step: 0.01 },
          { id: "oblique-far", label: "far clip", control: "number", defaultValue: 100, min: 1, step: 1 },
        ],
      },
    ],
  },
  {
    label: "Isometric",
    description: "fixed equal-axis orthographic",
    settingGroups: [
      {
        title: "Projection",
        settings: [
          { id: "iso-scale", label: "orthographic scale", control: "number", defaultValue: 4, min: 0.1, step: 0.1 },
          {
            id: "iso-orientation",
            label: "isometric orientation",
            control: "select",
            defaultValue: "standard",
            options: ["standard", "left", "right", "custom"],
          },
          { id: "iso-snap", label: "snap rotation", control: "toggle", defaultValue: true },
        ],
      },
      {
        title: "Clipping",
        settings: [
          { id: "iso-near", label: "near clip", control: "number", defaultValue: 0.1, min: 0.01, step: 0.01 },
          { id: "iso-far", label: "far clip", control: "number", defaultValue: 100, min: 1, step: 1 },
        ],
      },
    ],
  },
  {
    label: "Motion blur",
    description: "camera rays sampled across time",
    settingGroups: [
      {
        title: "Shutter",
        settings: [
          { id: "motion-shutter-open", label: "shutter open", control: "number", defaultValue: 0, min: 0, step: 0.01 },
          { id: "motion-shutter-close", label: "shutter close", control: "number", defaultValue: 0.5, min: 0, step: 0.01 },
          {
            id: "motion-time-distribution",
            label: "sample time distribution",
            control: "select",
            defaultValue: "uniform",
            options: ["uniform", "center-weighted", "custom curve"],
          },
        ],
      },
      {
        title: "Motion",
        settings: [
          {
            id: "motion-interpolation",
            label: "transform interpolation",
            control: "select",
            defaultValue: "linear",
            options: ["linear", "cubic", "step"],
          },
          {
            id: "motion-rolling-direction",
            label: "rolling shutter direction",
            control: "select",
            defaultValue: "none",
            options: ["none", "top to bottom", "left to right"],
          },
          { id: "motion-rolling-duration", label: "rolling shutter duration", control: "number", defaultValue: 0, min: 0, step: 0.01 },
        ],
      },
    ],
  },
  {
    label: "Physical",
    description: "sensor and exposure model",
    settingGroups: [
      {
        title: "Body",
        settings: [
          { id: "physical-focal-length", label: "focal length", control: "number", defaultValue: 50, min: 1, step: 1, suffix: "mm" },
          {
            id: "physical-sensor-size",
            label: "sensor size",
            control: "select",
            defaultValue: "36 x 24 mm",
            options: ["36 x 24 mm", "24 x 16 mm", "17.3 x 13 mm", "custom"],
          },
          {
            id: "physical-film-gate",
            label: "film gate",
            control: "select",
            defaultValue: "horizontal",
            options: ["horizontal", "vertical", "fill", "overscan"],
          },
          { id: "physical-crop-factor", label: "crop factor", control: "number", defaultValue: 1, min: 0.1, step: 0.1 },
        ],
      },
      {
        title: "Exposure",
        settings: [
          { id: "physical-fstop", label: "f-stop", control: "number", defaultValue: 2.8, min: 0.7, step: 0.1 },
          {
            id: "physical-shutter-speed",
            label: "shutter speed",
            control: "select",
            defaultValue: "1/125",
            options: ["1/30", "1/60", "1/125", "1/250", "1/500"],
          },
          { id: "physical-iso", label: "ISO-like exposure", control: "number", defaultValue: 100, min: 25, step: 25 },
        ],
      },
    ],
  },
];

function CameraModeInfo({ description }: { description: string }) {
  return (
    <Tooltip>
      <TooltipTrigger
        render={
          <span className="inline-flex size-4 shrink-0 items-center justify-center rounded-sm text-muted-foreground transition-colors hover:text-foreground" />
        }
      >
        <InfoIcon className="size-3" aria-hidden="true" />
      </TooltipTrigger>
      <TooltipContent side="right" align="center">
        {description}
      </TooltipContent>
    </Tooltip>
  );
}

const pipelineSections: PipelineSection[] = [
  {
    id: "camera",
    title: "Camera",
    icon: ApertureIcon,
    activeLabel: "Camera type",
    selectOptions: cameraModes.map((mode) => mode.label),
    groups: [],
    minimum: ["Pinhole", "Orthographic", "Thin lens"],
  },
  {
    id: "geometry",
    title: "Geometry",
    icon: BoxesIcon,
    activeLabel: "Primary geometry",
    selectOptions: [
      "sphere",
      "plane",
      "triangle",
      "quad",
      "box",
      "capsule",
      "cylinder",
      "cone",
      "disk",
      "torus",
    ],
    groups: [
      {
        title: "Primitive geometry",
        options: [
          "sphere",
          "plane",
          "triangle",
          "quad",
          "box",
          "capsule",
          "cylinder",
          "cone",
          "disk",
          "torus",
        ],
      },
      {
        title: "Mesh geometry",
        options: [
          "triangle mesh",
          "indexed triangle mesh",
          "instanced mesh",
          "skinned mesh",
          "displacement mesh",
          "subdivision mesh",
        ],
      },
      {
        title: "Procedural geometry",
        options: [
          "signed distance fields",
          "metaballs",
          "fractals",
          "heightfields",
          "voxels",
          "implicit surfaces",
          "constructive solid geometry",
        ],
      },
      {
        title: "Scene composition",
        options: [
          "static objects",
          "dynamic objects",
          "instanced objects",
          "animated objects",
          "emissive objects",
          "invisible-to-camera objects",
          "shadow-only objects",
        ],
      },
    ],
    minimum: ["sphere", "triangle", "triangle mesh", "instanced mesh"],
  },
  {
    id: "material",
    title: "Material",
    icon: PaletteIcon,
    activeLabel: "Default material",
    selectOptions: [
      "diffuse / Lambertian",
      "metal",
      "dielectric / glass",
      "emissive",
      "glossy",
      "rough conductor",
      "rough dielectric",
      "Disney principled",
      "clearcoat",
      "subsurface",
      "anisotropic",
      "translucent",
      "volume material",
      "procedural material",
    ],
    groups: [
      {
        title: "Material library",
        options: [
          "diffuse / Lambertian",
          "metal",
          "dielectric / glass",
          "emissive",
          "glossy",
          "rough conductor",
          "rough dielectric",
          "Disney principled",
          "clearcoat",
          "subsurface",
          "anisotropic",
          "translucent",
          "volume material",
          "procedural material",
        ],
      },
    ],
    minimum: ["diffuse / Lambertian", "metal", "dielectric / glass", "emissive"],
  },
  {
    id: "light",
    title: "Light",
    icon: LightbulbIcon,
    activeLabel: "Primary light",
    selectOptions: [
      "point light",
      "directional light",
      "spot light",
      "area light",
      "sphere light",
      "quad light",
      "triangle mesh light",
      "environment map",
      "sky model",
      "emissive geometry",
    ],
    groups: [
      {
        title: "Light library",
        options: [
          "point light",
          "directional light",
          "spot light",
          "area light",
          "sphere light",
          "quad light",
          "triangle mesh light",
          "environment map",
          "sky model",
          "emissive geometry",
        ],
      },
    ],
    minimum: ["emissive geometry", "quad light", "environment map"],
  },
  {
    id: "scene-loader",
    title: "Scene Loader",
    icon: FolderOpenIcon,
    activeLabel: "Loader",
    selectOptions: [
      "hardcoded scene",
      "JSON scene",
      "glTF",
      "OBJ",
      "procedural scene",
      "editor-authored scene",
      "benchmark scene",
    ],
    groups: [
      {
        title: "Sources",
        options: [
          "hardcoded scene",
          "JSON scene",
          "glTF",
          "OBJ",
          "procedural scene",
          "editor-authored scene",
          "benchmark scene",
        ],
      },
    ],
    minimum: ["hardcoded scene", "JSON scene", "glTF"],
  },
  {
    id: "tracer",
    title: "Tracer",
    icon: GitBranchIcon,
    activeLabel: "Trace backend",
    selectOptions: [
      "brute force",
      "uniform grid",
      "BVH",
      "wide BVH",
      "two-level BVH",
      "KD-tree",
      "octree",
      "bounding sphere hierarchy",
      "SDF ray marcher",
      "voxel DDA tracer",
      "heightfield tracer",
      "hardware ray tracing backend",
      "hybrid raster + ray query tracer",
    ],
    groups: [
      {
        title: "Basic tracers",
        options: [
          "brute force",
          "uniform grid",
          "BVH",
          "wide BVH",
          "two-level BVH",
          "KD-tree",
          "octree",
          "bounding sphere hierarchy",
        ],
      },
      {
        title: "Specialized tracers",
        options: [
          "SDF ray marcher",
          "voxel DDA tracer",
          "heightfield tracer",
          "hardware ray tracing backend",
          "hybrid raster + ray query tracer",
        ],
      },
      {
        title: "Debug tracers",
        options: [
          "first-hit only",
          "any-hit / shadow ray only",
          "closest-hit",
          "intersection count visualizer",
          "BVH depth visualizer",
          "traversal heatmap",
        ],
      },
    ],
    minimum: ["brute force", "BVH", "SDF ray marcher"],
  },
  {
    id: "sampler",
    title: "Sampler",
    icon: WavesIcon,
    activeLabel: "Pixel sampler",
    selectOptions: [
      "white noise",
      "frame-indexed white noise",
      "hash-based RNG",
      "PCG",
      "XorShift",
      "Wang hash",
      "stratified sampling",
      "jittered grid",
      "Latin hypercube",
      "Halton",
      "Hammersley",
      "Sobol",
      "Owen-scrambled Sobol",
      "blue noise",
      "tiled blue noise",
      "spatiotemporal blue noise",
      "Bayer matrix",
      "interleaved gradient noise",
    ],
    groups: [
      {
        title: "Basic samplers",
        options: [
          "white noise",
          "frame-indexed white noise",
          "hash-based RNG",
          "PCG",
          "XorShift",
          "Wang hash",
        ],
      },
      {
        title: "Structured samplers",
        options: [
          "stratified sampling",
          "jittered grid",
          "Latin hypercube",
          "Halton",
          "Hammersley",
          "Sobol",
          "Owen-scrambled Sobol",
        ],
      },
      {
        title: "Image-space samplers",
        options: [
          "blue noise",
          "tiled blue noise",
          "spatiotemporal blue noise",
          "Bayer matrix",
          "interleaved gradient noise",
        ],
      },
      {
        title: "Domain-specific samplers",
        options: [
          "hemisphere uniform",
          "hemisphere cosine-weighted",
          "sphere sampling",
          "disk sampling",
          "cone sampling",
          "GGX sampling",
          "light sampling",
          "environment map sampling",
          "triangle area sampling",
        ],
      },
    ],
    minimum: [
      "white noise",
      "blue noise",
      "hemisphere cosine-weighted",
      "GGX sampling",
      "light sampling",
    ],
  },
  {
    id: "integrator",
    title: "Integrator",
    icon: SunIcon,
    activeLabel: "Integrator",
    selectOptions: [
      "solid color",
      "normal view",
      "depth view",
      "albedo view",
      "UV view",
      "material ID view",
      "primitive ID view",
      "world position view",
      "bounce count view",
      "ray distance view",
      "heatmap view",
      "ambient only",
      "direct lighting",
      "direct lighting with shadows",
      "ambient occlusion",
      "path tracing",
      "path tracing with next-event estimation",
      "path tracing with multiple importance sampling",
      "bidirectional path tracing",
      "volumetric path tracing",
      "photon mapping",
      "ReSTIR direct lighting",
      "ReSTIR global illumination",
    ],
    groups: [
      {
        title: "Debug integrators",
        options: [
          "solid color",
          "normal view",
          "depth view",
          "albedo view",
          "UV view",
          "material ID view",
          "primitive ID view",
          "world position view",
          "bounce count view",
          "ray distance view",
          "heatmap view",
        ],
      },
      {
        title: "Lighting integrators",
        options: [
          "ambient only",
          "direct lighting",
          "direct lighting with shadows",
          "ambient occlusion",
          "path tracing",
          "path tracing with next-event estimation",
          "path tracing with multiple importance sampling",
          "bidirectional path tracing",
          "volumetric path tracing",
          "photon mapping",
          "ReSTIR direct lighting",
          "ReSTIR global illumination",
        ],
      },
      {
        title: "Special effects integrators",
        options: [
          "reflection-only",
          "refraction-only",
          "caustics experiment",
          "participating media",
          "subsurface scattering",
          "spectral rendering",
          "toon/path hybrid",
        ],
      },
    ],
    minimum: [
      "normal view",
      "depth view",
      "direct lighting",
      "path tracing",
      "path tracing with next-event estimation",
    ],
  },
  {
    id: "denoiser",
    title: "Denoiser",
    icon: SparklesIcon,
    activeLabel: "Denoiser",
    selectOptions: [
      "none",
      "simple temporal accumulation",
      "exponential moving average",
      "accumulation with reset",
      "accumulation with variance estimate",
      "box blur",
      "Gaussian blur",
      "bilateral filter",
      "a-trous wavelet filter",
      "edge-aware filter",
      "normal/depth-aware filter",
      "temporal reprojection",
      "temporal anti-aliasing",
      "history clamping",
      "variance-guided temporal filtering",
      "SVGF",
      "A-SVGF",
      "ReBLUR-style denoiser",
      "neural denoiser",
      "path-space denoiser",
    ],
    groups: [
      {
        title: "No-op / accumulation",
        options: [
          "none",
          "simple temporal accumulation",
          "exponential moving average",
          "accumulation with reset",
          "accumulation with variance estimate",
        ],
      },
      {
        title: "Spatial filters",
        options: [
          "box blur",
          "Gaussian blur",
          "bilateral filter",
          "a-trous wavelet filter",
          "edge-aware filter",
          "normal/depth-aware filter",
        ],
      },
      {
        title: "Temporal filters",
        options: [
          "temporal reprojection",
          "temporal anti-aliasing",
          "history clamping",
          "variance-guided temporal filtering",
        ],
      },
      {
        title: "Advanced denoisers",
        options: [
          "SVGF",
          "A-SVGF",
          "ReBLUR-style denoiser",
          "neural denoiser",
          "path-space denoiser",
        ],
      },
      {
        title: "Auxiliary buffers",
        options: [
          "depth",
          "normal",
          "albedo",
          "motion vectors",
          "material ID",
          "roughness",
          "variance",
          "sample count",
        ],
      },
    ],
    minimum: ["none", "simple temporal accumulation", "bilateral filter", "SVGF"],
  },
  {
    id: "tonemapper",
    title: "Tonemapper",
    icon: ContrastIcon,
    activeLabel: "Curve",
    selectOptions: [
      "linear",
      "clamp",
      "Reinhard",
      "extended Reinhard",
      "exposure only",
      "gamma correction",
      "ACES approximation",
      "Uncharted 2 filmic",
      "Hable",
      "AgX",
      "Khronos PBR Neutral",
      "custom filmic curve",
    ],
    groups: [
      {
        title: "Basic tonemappers",
        options: [
          "linear",
          "clamp",
          "Reinhard",
          "extended Reinhard",
          "exposure only",
          "gamma correction",
        ],
      },
      {
        title: "Filmic tonemappers",
        options: [
          "ACES approximation",
          "Uncharted 2 filmic",
          "Hable",
          "AgX",
          "Khronos PBR Neutral",
          "custom filmic curve",
        ],
      },
      {
        title: "Color pipeline",
        options: [
          "exposure",
          "white balance",
          "contrast",
          "saturation",
          "gamma",
          "color grading LUT",
          "bloom combine",
          "vignette",
        ],
      },
      {
        title: "Output formats",
        options: ["sRGB", "linear RGB", "HDR10-ish output", "display P3", "Rec.709"],
      },
    ],
    minimum: ["linear", "Reinhard", "ACES approximation", "exposure", "gamma correction"],
  },
  {
    id: "render-target",
    title: "Render Target",
    icon: FrameIcon,
    activeLabel: "Target mode",
    selectOptions: [
      "full resolution",
      "half resolution",
      "tiled rendering",
      "progressive rendering",
      "checkerboard rendering",
      "adaptive resolution",
      "fixed sample budget",
    ],
    groups: [
      {
        title: "Target modes",
        options: [
          "full resolution",
          "half resolution",
          "tiled rendering",
          "progressive rendering",
          "checkerboard rendering",
          "adaptive resolution",
          "fixed sample budget",
        ],
      },
    ],
    minimum: ["full resolution", "progressive rendering"],
  },
  {
    id: "display",
    title: "Display",
    icon: MonitorIcon,
    activeLabel: "Viewport",
    selectOptions: ["canvas viewport", "fullscreen preview", "split compare", "histogram overlay"],
    groups: [
      {
        title: "Viewport overlays",
        options: [
          "sample count",
          "frame time",
          "exposure readout",
          "pixel inspector",
          "false-color legend",
        ],
      },
    ],
    minimum: ["canvas viewport", "sample count"],
  },
  {
    id: "debug-views",
    title: "Debug Views",
    icon: BugIcon,
    activeLabel: "Debug view",
    selectOptions: [
      "none",
      "normal view",
      "depth view",
      "albedo view",
      "material ID view",
      "primitive ID view",
      "BVH depth visualizer",
      "traversal heatmap",
    ],
    groups: [
      {
        title: "Inspection overlays",
        options: [
          "normal view",
          "depth view",
          "albedo view",
          "material ID view",
          "primitive ID view",
          "world position view",
          "bounce count view",
          "ray distance view",
          "BVH depth visualizer",
          "traversal heatmap",
        ],
      },
    ],
    minimum: ["none", "normal view", "depth view"],
  },
];

function getInitialActiveOptions() {
  return Object.fromEntries(
    pipelineSections.map((section) => [
      section.id,
      section.selectOptions.find((option) => section.minimum.includes(option)) ??
        section.selectOptions[0],
    ]),
  );
}

function getInitialEnabledOptions() {
  return Object.fromEntries(
    pipelineSections.map((section) => {
      if (section.id === "camera") {
        return [section.id, []];
      }

      return [
        section.id,
        section.groups
          .flatMap((group) => group.options)
          .filter((option) => section.minimum.includes(option)),
      ];
    }),
  );
}

function getInitialCameraSettingValues() {
  return Object.fromEntries(
    cameraModes.flatMap((mode) =>
      mode.settingGroups.flatMap((group) =>
        group.settings.map((setting) => [setting.id, setting.defaultValue]),
      ),
    ),
  );
}

function CameraSettingInput({
  setting,
  value,
  onChange,
}: {
  setting: CameraSetting;
  value: CameraSettingValue;
  onChange: (value: CameraSettingValue) => void;
}) {
  if (setting.control === "select") {
    return (
      <Select
        value={String(value)}
        onValueChange={(nextValue) => {
          if (nextValue === null) {
            return;
          }

          onChange(nextValue);
        }}
      >
        <SelectTrigger size="sm" className="h-7 w-32 bg-background/60 text-[11px]">
          <SelectValue />
        </SelectTrigger>
        <SelectContent align="end" className="max-h-64">
          <SelectGroup>
            <SelectLabel>{setting.label}</SelectLabel>
            {setting.options.map((option) => (
              <SelectItem key={option} value={option} className="text-xs">
                {option}
              </SelectItem>
            ))}
          </SelectGroup>
        </SelectContent>
      </Select>
    );
  }

  if (setting.control === "toggle") {
    return (
      <Switch
        size="sm"
        checked={Boolean(value)}
        onCheckedChange={(nextValue) => onChange(nextValue === true)}
        aria-label={setting.label}
      />
    );
  }

  if (setting.control === "number") {
    return (
      <span className="flex shrink-0 items-center gap-1">
        <Input
          type="number"
          value={Number(value)}
          min={setting.min}
          max={setting.max}
          step={setting.step}
          onChange={(event) => onChange(Number(event.currentTarget.value))}
          className="h-7 w-20 bg-background/60 px-2 text-right text-[11px]"
          aria-label={setting.label}
        />
        {setting.suffix ? (
          <span className="w-6 text-[10px] text-muted-foreground">{setting.suffix}</span>
        ) : null}
      </span>
    );
  }

  return (
    <Input
      value={String(value)}
      onChange={(event) => onChange(event.currentTarget.value)}
      className="h-7 w-32 bg-background/60 px-2 text-[11px]"
      aria-label={setting.label}
    />
  );
}

export function AppSidebar() {
  const [activeOptions, setActiveOptions] =
    useState<Record<string, string>>(getInitialActiveOptions);
  const [enabledOptions, setEnabledOptions] =
    useState<Record<string, string[]>>(getInitialEnabledOptions);
  const [cameraSettingValues, setCameraSettingValues] =
    useState<Record<string, CameraSettingValue>>(getInitialCameraSettingValues);

  const updateCameraSetting = (settingId: string, value: CameraSettingValue) => {
    setCameraSettingValues((current) => ({
      ...current,
      [settingId]: value,
    }));
  };

  const toggleEnabledOption = (sectionId: string, option: string, checked: boolean) => {
    setEnabledOptions((current) => {
      const values = current[sectionId] ?? [];
      const nextValues = checked
        ? Array.from(new Set([...values, option]))
        : values.filter((value) => value !== option);

      return {
        ...current,
        [sectionId]: nextValues,
      };
    });
  };

  return (
    <Sidebar collapsible="none" className="hidden h-svh border-r-0 bg-muted/60 md:flex">
      <SidebarHeader className="gap-3 px-3 pt-4 pb-3">
        <SidebarMenu>
          <SidebarMenuItem>
            <DropdownMenu>
              <DropdownMenuTrigger
                render={
                  <SidebarMenuButton
                    size="lg"
                    className="h-8 gap-2 rounded-md px-1.5 data-open:bg-sidebar-accent data-open:text-sidebar-accent-foreground"
                  />
                }
              >
                <Avatar size="sm">
                  <AvatarFallback>ID</AvatarFallback>
                </Avatar>
                <span className="truncate text-sm font-medium">idobbins</span>
                <ChevronDownIcon className="ml-auto size-3.5 text-sidebar-foreground/50" />
              </DropdownMenuTrigger>
              <DropdownMenuContent side="bottom" align="start" className="w-48 p-1.5">
                <DropdownMenuGroup>
                  <DropdownMenuItem>
                    <Layers3Icon />
                    <span>Pipeline presets</span>
                  </DropdownMenuItem>
                  <DropdownMenuItem>
                    <UserPlusIcon />
                    <span>Invite people</span>
                  </DropdownMenuItem>
                </DropdownMenuGroup>
                <DropdownMenuSeparator />
                <DropdownMenuGroup>
                  <DropdownMenuItem variant="destructive">
                    <LogOutIcon />
                    <span>Log out</span>
                  </DropdownMenuItem>
                </DropdownMenuGroup>
              </DropdownMenuContent>
            </DropdownMenu>
          </SidebarMenuItem>
        </SidebarMenu>
      </SidebarHeader>
      <SidebarContent>
        <div className="px-3 pb-4 pt-1">
          <div className="mb-2 px-2 text-xs text-muted-foreground">
            <span>Render pipeline</span>
          </div>

          <Accordion multiple defaultValue={["camera"]}>
            {pipelineSections.map((section) => {
              const activeOption = activeOptions[section.id];
              const enabledValues = enabledOptions[section.id] ?? [];
              const activeCameraMode =
                section.id === "camera"
                  ? cameraModes.find((mode) => mode.label === activeOption) ?? cameraModes[0]
                  : undefined;

              return (
                <AccordionItem
                  key={section.id}
                  value={section.id}
                  className="border-b border-sidebar-border/70 last:border-b-0"
                >
                  <AccordionTrigger className="min-h-9 gap-2 rounded-md px-2 py-1.5 text-sidebar-foreground hover:bg-sidebar-accent hover:no-underline">
                    <span className="flex min-w-0 flex-1 items-center gap-2">
                      <section.icon className="size-3.5 shrink-0 text-sidebar-foreground/55" />
                      <span className="min-w-0 flex-1 truncate text-[13px]">{section.title}</span>
                      <span className="max-w-34 truncate text-[11px] font-normal text-muted-foreground">
                        {activeOption}
                      </span>
                    </span>
                  </AccordionTrigger>
                  <AccordionContent className="space-y-3 px-2 pb-3">
                    {activeCameraMode ? (
                      <>
                        <div className="grid gap-1.5">
                          <span className="text-[11px] font-medium text-muted-foreground">
                            Camera type
                          </span>
                          <DropdownMenu>
                            <DropdownMenuTrigger
                              render={
                                <button
                                  type="button"
                                  className="flex min-h-10 w-full cursor-pointer items-center gap-2 rounded-md border border-sidebar-border bg-background/75 px-2 py-1.5 text-left text-sidebar-foreground transition-colors hover:bg-sidebar-accent hover:text-sidebar-accent-foreground focus-visible:border-ring focus-visible:outline-none"
                                />
                              }
                            >
                              <span className="flex min-w-0 flex-1 items-center gap-1.5">
                                <span className="min-w-0 truncate text-[12px] font-medium">
                                  {activeCameraMode.label}
                                </span>
                                <CameraModeInfo description={activeCameraMode.description} />
                              </span>
                              <ChevronDownIcon className="size-3.5 shrink-0 text-muted-foreground" />
                            </DropdownMenuTrigger>
                            <DropdownMenuContent align="start" className="max-h-80 w-72">
                              <DropdownMenuGroup>
                                {cameraModes.map((mode) => {
                                  const selected = activeOption === mode.label;

                                  return (
                                    <DropdownMenuItem
                                      key={mode.label}
                                      data-selected={selected}
                                      onClick={() => {
                                        setActiveOptions((current) => ({
                                          ...current,
                                          [section.id]: mode.label,
                                        }));
                                      }}
                                      className="gap-2 py-1.5 text-xs data-[selected=true]:bg-accent/70 data-[selected=true]:text-accent-foreground"
                                    >
                                      <span
                                        aria-hidden="true"
                                        data-selected={selected}
                                        className="size-1.5 shrink-0 rounded-full bg-transparent data-[selected=true]:bg-current"
                                      />
                                      <span className="flex min-w-0 flex-1 items-center gap-1.5">
                                        <span className="min-w-0 truncate font-medium">
                                          {mode.label}
                                        </span>
                                        <CameraModeInfo description={mode.description} />
                                      </span>
                                    </DropdownMenuItem>
                                  );
                                })}
                              </DropdownMenuGroup>
                            </DropdownMenuContent>
                          </DropdownMenu>
                        </div>

                        <div className="grid gap-2">
                          <div className="flex items-center justify-between gap-2">
                            <span className="text-[10px] font-medium tracking-wide text-muted-foreground uppercase">
                              Settings
                            </span>
                            <Badge variant="outline" className="h-4 px-1.5 text-[10px]">
                              {
                                activeCameraMode.settingGroups
                                  .flatMap((group) => group.settings)
                                  .length
                              }
                            </Badge>
                          </div>
                          {activeCameraMode.settingGroups.map((group) => {
                            return (
                              <div
                                key={group.title}
                                className="grid gap-1 border-t border-sidebar-border/50 pt-2 first:border-t-0 first:pt-0"
                              >
                                <div className="flex items-center justify-between gap-2 px-0.5">
                                  <span className="text-[12px] text-sidebar-foreground/75">
                                    {group.title}
                                  </span>
                                </div>
                                <div className="grid gap-1">
                                  {group.settings.map((setting) => {
                                    return (
                                      <div
                                        key={setting.id}
                                        className="flex min-h-8 items-center justify-between gap-3 rounded-md px-2 py-1 text-[12px] text-sidebar-foreground/80 transition-colors hover:bg-sidebar-accent/60"
                                      >
                                        <span
                                          className="min-w-0 flex-1 truncate"
                                          title={setting.label}
                                        >
                                          {setting.label}
                                        </span>
                                        <CameraSettingInput
                                          setting={setting}
                                          value={
                                            cameraSettingValues[setting.id] ?? setting.defaultValue
                                          }
                                          onChange={(value) =>
                                            updateCameraSetting(setting.id, value)
                                          }
                                        />
                                      </div>
                                    );
                                  })}
                                </div>
                              </div>
                            );
                          })}
                        </div>
                      </>
                    ) : (
                      <>
                        <div className="grid gap-1.5">
                          <span className="text-[11px] font-medium text-muted-foreground">
                            {section.activeLabel}
                          </span>
                          <Select
                            value={activeOption}
                            onValueChange={(option) => {
                              if (option === null) {
                                return;
                              }

                              setActiveOptions((current) => ({
                                ...current,
                                [section.id]: option,
                              }));
                            }}
                          >
                            <SelectTrigger size="sm" className="w-full bg-background/60 text-xs">
                              <SelectValue />
                            </SelectTrigger>
                            <SelectContent align="start" className="max-h-72">
                              <SelectGroup>
                                <SelectLabel>{section.activeLabel}</SelectLabel>
                                {section.selectOptions.map((option) => (
                                  <SelectItem key={option} value={option} className="text-xs">
                                    {option}
                                  </SelectItem>
                                ))}
                              </SelectGroup>
                            </SelectContent>
                          </Select>
                        </div>

                        <div className="grid gap-1 rounded-md bg-muted/40 px-2 py-1.5">
                          <div className="flex items-center justify-between gap-2">
                            <span className="text-[10px] font-medium tracking-wide text-muted-foreground uppercase">
                              Minimum
                            </span>
                            <Badge variant="outline" className="h-4 px-1.5 text-[10px]">
                              {section.minimum.length}
                            </Badge>
                          </div>
                          <p className="text-[11px] leading-4 text-muted-foreground">
                            {section.minimum.join(", ")}
                          </p>
                        </div>

                        <SidebarGroupContent>
                          <Accordion multiple className="gap-0">
                            {section.groups.map((group) => {
                              const enabledCount = group.options.filter((option) =>
                                enabledValues.includes(option),
                              ).length;

                              return (
                                <AccordionItem
                                  key={group.title}
                                  value={group.title}
                                  className="border-b border-sidebar-border/50 last:border-b-0"
                                >
                                  <AccordionTrigger className="min-h-8 rounded-md px-0.5 py-1.5 text-[12px] text-sidebar-foreground/75 hover:bg-sidebar-accent hover:no-underline">
                                    <span className="flex min-w-0 flex-1 items-center gap-2">
                                      <span className="min-w-0 flex-1 truncate">
                                        {group.title}
                                      </span>
                                      <span className="shrink-0 text-[11px] font-normal text-muted-foreground">
                                        {enabledCount}/{group.options.length}
                                      </span>
                                    </span>
                                  </AccordionTrigger>
                                  <AccordionContent className="grid gap-1 pb-2">
                                    {group.options.map((option) => {
                                      const checked = enabledValues.includes(option);

                                      return (
                                        <label
                                          key={option}
                                          data-selected={checked}
                                          className="flex min-h-7 cursor-pointer items-center gap-2 rounded-md px-2 py-1 text-[12px] text-sidebar-foreground/80 transition-colors hover:bg-sidebar-accent hover:text-sidebar-accent-foreground data-[selected=true]:bg-sidebar-accent/70 data-[selected=true]:text-sidebar-accent-foreground"
                                        >
                                          <Checkbox
                                            checked={checked}
                                            onCheckedChange={(nextChecked) =>
                                              toggleEnabledOption(
                                                section.id,
                                                option,
                                                nextChecked === true,
                                              )
                                            }
                                            className="size-3.5 rounded-[3px]"
                                            aria-label={option}
                                          />
                                          <span className="min-w-0 flex-1 truncate" title={option}>
                                            {option}
                                          </span>
                                        </label>
                                      );
                                    })}
                                  </AccordionContent>
                                </AccordionItem>
                              );
                            })}
                          </Accordion>
                        </SidebarGroupContent>
                      </>
                    )}
                  </AccordionContent>
                </AccordionItem>
              );
            })}
          </Accordion>
        </div>
      </SidebarContent>
    </Sidebar>
  );
}
