import { Input } from "@/components/ui/input";
import { Select, SelectContent, SelectGroup, SelectItem, SelectLabel, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Switch } from "@/components/ui/switch";
import type { CameraSetting, CameraSettingValue } from "@/lib/pipeline-types";

export function CameraSettingInput({
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
