"use client";

import { ContrastIcon, GaugeIcon } from "lucide-react";

import { Accordion, AccordionContent, AccordionItem } from "@/components/ui/accordion";
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectLabel,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Tabs, TabsList, TabsTrigger } from "@/components/ui/tabs";
import {
  recordingProfiles,
  type RecordingCodec,
  type RecordingCodecId,
  type RecordingProfileId,
} from "@/lib/recording-settings";
import {
  renderAspectRatioProfiles,
  toneMapOptions,
  type ComparisonMode,
  type RenderAspectRatio,
  type RenderQuality,
  type RenderSettings,
  type ToneMap,
} from "@/lib/render-settings";

import { CameraSettings } from "./camera-settings";
import { SamplingSettings } from "./sampling-settings";
import { SceneSettings } from "./scene-settings";
import type { RecordingState } from "./sidebar-header-actions";
import { SettingRow } from "./setting-row";
import { SidebarSectionTrigger, type RenderSettingChange } from "./sidebar-section";

const previewQualityOptions: Array<{
  label: string;
  value: RenderQuality;
  maxPixels: number;
  detail: string;
}> = [
  { label: "Draft", value: "draft", maxPixels: 400_000, detail: "fastest preview" },
  { label: "Balanced", value: "balanced", maxPixels: 1_600_000, detail: "default preview" },
  { label: "High", value: "high", maxPixels: 2_400_000, detail: "sharper preview" },
  {
    label: "Native",
    value: "native",
    maxPixels: Number.MAX_SAFE_INTEGER,
    detail: "full viewport",
  },
];
const comparisonModeTabs: Array<{ value: ComparisonMode; label: string; detail: string }> = [
  { value: "inline-split", label: "Inline", detail: "one framed view cut in half" },
  { value: "side-by-side", label: "Side", detail: "two independent panes" },
  { value: "swap", label: "Swap", detail: "show the selected pane" },
];

function RenderOutputSection({
  settings,
  onSettingChange,
  recordingState,
  recordingProfileId,
  onRecordingProfileChange,
  recordingCodecId,
  onRecordingCodecChange,
  supportedRecordingCodecs,
  comparisonMode,
  onComparisonModeChange,
}: {
  settings: RenderSettings;
  onSettingChange: RenderSettingChange<RenderSettings>;
  recordingState: RecordingState;
  recordingProfileId: RecordingProfileId;
  onRecordingProfileChange: (profileId: RecordingProfileId) => void;
  recordingCodecId: RecordingCodecId;
  onRecordingCodecChange: (codecId: RecordingCodecId) => void;
  supportedRecordingCodecs: RecordingCodec[];
  comparisonMode: ComparisonMode;
  onComparisonModeChange: (mode: ComparisonMode) => void;
}) {
  const activeQualityLabel =
    previewQualityOptions.find((option) => option.value === settings.renderQuality)?.label ??
    `${Math.round(settings.maxPixels / 1000)}k px`;
  const activeAspectRatio =
    renderAspectRatioProfiles.find((profile) => profile.value === settings.renderAspectRatio) ??
    renderAspectRatioProfiles[0];
  const sectionValue = `${activeQualityLabel} / ${activeAspectRatio.label}`;

  const setPreviewQuality = (quality: RenderQuality) => {
    const option = previewQualityOptions.find((candidate) => candidate.value === quality);

    if (!option) {
      return;
    }

    onSettingChange("renderQuality", option.value);
    onSettingChange("maxPixels", option.maxPixels);
  };

  return (
    <AccordionItem value="render" className="border-b border-sidebar-border/70 last:border-b-0">
      <SidebarSectionTrigger icon={GaugeIcon} title="Render Output" value={sectionValue} />
      <AccordionContent className="space-y-3 px-2 pb-3">
        <div className="grid gap-2">
          <SettingRow label="view">
            <Tabs
              value={comparisonMode}
              onValueChange={(value) => onComparisonModeChange(value as ComparisonMode)}
              className="gap-0"
              aria-label="Render view mode"
            >
              <TabsList className="grid h-7 w-36 grid-cols-3">
                {comparisonModeTabs.map((tab) => (
                  <TabsTrigger
                    key={tab.value}
                    value={tab.value}
                    title={tab.detail}
                    className="px-1 text-[11px]"
                  >
                    {tab.label}
                  </TabsTrigger>
                ))}
              </TabsList>
            </Tabs>
          </SettingRow>

          <SettingRow label="recording">
            <div className="flex items-center gap-1.5">
              <Select
                value={recordingProfileId}
                onValueChange={(value) => onRecordingProfileChange(value as RecordingProfileId)}
                disabled={recordingState !== "idle"}
              >
                <SelectTrigger size="sm" className="h-7 w-28 bg-background/60 text-[11px]">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent align="end" alignItemWithTrigger={false} sideOffset={8}>
                  <SelectGroup>
                    <SelectLabel>Recording quality</SelectLabel>
                    {recordingProfiles.map((profile) => (
                      <SelectItem
                        key={profile.id}
                        value={profile.id}
                        title={profile.detail}
                        className="text-xs"
                      >
                        {profile.menuLabel}
                      </SelectItem>
                    ))}
                  </SelectGroup>
                </SelectContent>
              </Select>
              <Select
                value={recordingCodecId}
                onValueChange={(value) => onRecordingCodecChange(value as RecordingCodecId)}
                disabled={recordingState !== "idle" || supportedRecordingCodecs.length === 0}
              >
                <SelectTrigger size="sm" className="h-7 w-20 bg-background/60 text-[11px]">
                  <SelectValue placeholder="Codec" />
                </SelectTrigger>
                <SelectContent align="end" alignItemWithTrigger={false} sideOffset={8}>
                  <SelectGroup>
                    <SelectLabel>Codec</SelectLabel>
                    {supportedRecordingCodecs.map((codec) => (
                      <SelectItem
                        key={codec.id}
                        value={codec.id}
                        title={codec.detail}
                        className="text-xs"
                      >
                        {codec.menuLabel}
                      </SelectItem>
                    ))}
                  </SelectGroup>
                </SelectContent>
              </Select>
            </div>
          </SettingRow>

          <SettingRow label="tone map">
            <Select
              value={settings.toneMap}
              onValueChange={(value) => onSettingChange("toneMap", value as ToneMap)}
            >
              <SelectTrigger size="sm" className="h-7 w-28 bg-background/60 text-[11px]">
                <ContrastIcon className="size-3" />
                <SelectValue />
              </SelectTrigger>
              <SelectContent align="end" alignItemWithTrigger={false} sideOffset={8}>
                <SelectGroup>
                  <SelectLabel>Tone map</SelectLabel>
                  {toneMapOptions.map((option) => (
                    <SelectItem key={option.value} value={option.value} className="text-xs">
                      {option.label}
                    </SelectItem>
                  ))}
                </SelectGroup>
              </SelectContent>
            </Select>
          </SettingRow>

          <SettingRow label="quality">
            <Select
              value={settings.renderQuality}
              onValueChange={(value) => setPreviewQuality(value as RenderQuality)}
            >
              <SelectTrigger size="sm" className="h-7 w-28 bg-background/60 text-[11px]">
                <SelectValue />
              </SelectTrigger>
              <SelectContent align="end" alignItemWithTrigger={false} sideOffset={8}>
                <SelectGroup>
                  <SelectLabel>Preview quality</SelectLabel>
                  {previewQualityOptions.map((option) => (
                    <SelectItem
                      key={option.value}
                      value={option.value}
                      title={option.detail}
                      className="text-xs"
                    >
                      {option.label}
                    </SelectItem>
                  ))}
                </SelectGroup>
              </SelectContent>
            </Select>
          </SettingRow>
          <SettingRow label="aspect ratio">
            <Select
              value={settings.renderAspectRatio}
              onValueChange={(value) =>
                onSettingChange("renderAspectRatio", value as RenderAspectRatio)
              }
            >
              <SelectTrigger size="sm" className="h-7 w-28 bg-background/60 text-[11px]">
                <SelectValue />
              </SelectTrigger>
              <SelectContent align="end" alignItemWithTrigger={false} sideOffset={8}>
                <SelectGroup>
                  <SelectLabel>Aspect ratio</SelectLabel>
                  {renderAspectRatioProfiles.map((profile) => (
                    <SelectItem key={profile.value} value={profile.value} className="text-xs">
                      {profile.label}
                    </SelectItem>
                  ))}
                </SelectGroup>
              </SelectContent>
            </Select>
          </SettingRow>
        </div>
      </AccordionContent>
    </AccordionItem>
  );
}

export function SharedSessionAccordion({
  settings,
  onSettingChange,
  recordingState,
  recordingProfileId,
  onRecordingProfileChange,
  recordingCodecId,
  onRecordingCodecChange,
  supportedRecordingCodecs,
  comparisonMode,
  onComparisonModeChange,
}: {
  settings: RenderSettings;
  onSettingChange: RenderSettingChange<RenderSettings>;
  recordingState: RecordingState;
  recordingProfileId: RecordingProfileId;
  onRecordingProfileChange: (profileId: RecordingProfileId) => void;
  recordingCodecId: RecordingCodecId;
  onRecordingCodecChange: (codecId: RecordingCodecId) => void;
  supportedRecordingCodecs: RecordingCodec[];
  comparisonMode: ComparisonMode;
  onComparisonModeChange: (mode: ComparisonMode) => void;
}) {
  return (
    <Accordion multiple defaultValue={["scene", "camera", "render"]}>
      <SceneSettings settings={settings} onSettingChange={onSettingChange} />
      <CameraSettings settings={settings} onSettingChange={onSettingChange} />
      <RenderOutputSection
        settings={settings}
        onSettingChange={onSettingChange}
        recordingState={recordingState}
        recordingProfileId={recordingProfileId}
        onRecordingProfileChange={onRecordingProfileChange}
        recordingCodecId={recordingCodecId}
        onRecordingCodecChange={onRecordingCodecChange}
        supportedRecordingCodecs={supportedRecordingCodecs}
        comparisonMode={comparisonMode}
        onComparisonModeChange={onComparisonModeChange}
      />
    </Accordion>
  );
}

export function VariantPipelineAccordion({
  settings,
  onSettingChange,
}: {
  settings: RenderSettings;
  onSettingChange: RenderSettingChange<RenderSettings>;
}) {
  return (
    <Accordion multiple defaultValue={["sampling"]}>
      <SamplingSettings settings={settings} onSettingChange={onSettingChange} />
    </Accordion>
  );
}
