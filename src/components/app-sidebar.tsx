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
import { Kbd } from "@/components/ui/kbd";
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

type OptionGroup = {
  title: string;
  options: string[];
};

type PipelineSection = {
  id: string;
  title: string;
  icon: LucideIcon;
  contract: string;
  activeLabel: string;
  selectOptions: string[];
  groups: OptionGroup[];
  minimum: string[];
};

const pipelineSections: PipelineSection[] = [
  {
    id: "camera",
    title: "Camera",
    icon: ApertureIcon,
    contract: "pixel + sample -> ray",
    activeLabel: "Camera model",
    selectOptions: [
      "pinhole camera",
      "orthographic camera",
      "thin lens / depth of field camera",
      "fisheye camera",
      "panoramic / equirectangular camera",
      "cubemap camera",
      "stereo / VR camera",
      "oblique projection camera",
      "isometric camera",
      "camera with motion blur",
      "physical camera",
    ],
    groups: [
      {
        title: "Physical controls",
        options: ["focal length", "sensor size", "aperture", "shutter speed", "ISO-like exposure"],
      },
      {
        title: "Debug cameras",
        options: [
          "fixed axis camera",
          "light-view camera",
          "BVH inspection camera",
          "shadow camera",
          "scene bounds camera",
        ],
      },
    ],
    minimum: ["pinhole camera", "orthographic camera", "thin lens / depth of field camera"],
  },
  {
    id: "geometry",
    title: "Geometry",
    icon: BoxesIcon,
    contract: "ray -> closest hit",
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
    contract: "hit + incoming ray + sampler -> scatter/event",
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
    contract: "sample light -> direction + radiance + pdf",
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
    contract: "source asset -> scene data",
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
    contract: "trace(ray) -> hit",
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
    contract: "sampleN(seed, dimension) -> random-ish value",
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
    contract: "ray + scene + sampler -> color",
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
    contract: "noisy image + auxiliary buffers -> cleaner image",
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
    contract: "HDR color -> display color",
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
    contract: "render output -> storage/display buffer",
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
    contract: "display color -> viewport",
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
    contract: "buffers + scene metadata -> inspection overlay",
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
    pipelineSections.map((section) => [
      section.id,
      section.groups
        .flatMap((group) => group.options)
        .filter((option) => section.minimum.includes(option)),
    ]),
  );
}

export function AppSidebar() {
  const [activeOptions, setActiveOptions] =
    useState<Record<string, string>>(getInitialActiveOptions);
  const [enabledOptions, setEnabledOptions] =
    useState<Record<string, string[]>>(getInitialEnabledOptions);

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
                    <div className="grid gap-1">
                      <span className="text-[10px] font-medium tracking-wide text-muted-foreground uppercase">
                        Contract
                      </span>
                      <Kbd className="h-auto w-full justify-start px-2 py-1 font-mono text-[11px] whitespace-normal">
                        {section.contract}
                      </Kbd>
                    </div>

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
                                  <span className="min-w-0 flex-1 truncate">{group.title}</span>
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
