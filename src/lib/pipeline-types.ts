import type { LucideIcon } from "lucide-react";

export type OptionGroup = {
  title: string;
  options: string[];
};

export type PipelineSection = {
  id: string;
  title: string;
  icon: LucideIcon;
  activeLabel: string;
  selectOptions: string[];
  groups: OptionGroup[];
  minimum: string[];
};

export type CameraMode = {
  label: string;
  description: string;
  settingGroups: CameraSettingGroup[];
};

export type CameraSettingGroup = {
  title: string;
  settings: CameraSetting[];
};

export type CameraSetting =
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

export type CameraSettingValue = CameraSetting["defaultValue"];
