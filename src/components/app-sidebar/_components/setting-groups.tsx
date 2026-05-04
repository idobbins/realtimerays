import { Badge } from "@/components/ui/badge";
import { CameraSettingInput } from "@/components/app-sidebar/_components/setting-input";
import type { CameraSettingGroup, CameraSettingValue } from "@/lib/pipeline-types";

export function SettingGroups({
  groups,
  values,
  onSettingChange,
}: {
  groups: CameraSettingGroup[];
  values: Record<string, CameraSettingValue>;
  onSettingChange: (settingId: string, value: CameraSettingValue) => void;
}) {
  return (
    <div className="grid gap-2">
      <div className="flex items-center justify-between gap-2">
        <span className="text-[10px] font-medium tracking-wide text-muted-foreground uppercase">
          Settings
        </span>
        <Badge variant="outline" className="h-4 px-1.5 text-[10px]">
          {groups.flatMap((group) => group.settings).length}
        </Badge>
      </div>
      {groups.map((group) => (
        <div
          key={group.title}
          className="grid gap-1 border-t border-sidebar-border/50 pt-2 first:border-t-0 first:pt-0"
        >
          <div className="flex items-center justify-between gap-2 px-0.5">
            <span className="text-[12px] text-sidebar-foreground/75">{group.title}</span>
          </div>
          <div className="grid gap-1">
            {group.settings.map((setting) => (
              <div
                key={setting.id}
                className="flex min-h-8 items-center justify-between gap-3 rounded-md px-2 py-1 text-[12px] text-sidebar-foreground/80 transition-colors hover:bg-sidebar-accent/60"
              >
                <span className="min-w-0 flex-1 truncate" title={setting.label}>
                  {setting.label}
                </span>
                <CameraSettingInput
                  setting={setting}
                  value={values[setting.id] ?? setting.defaultValue}
                  onChange={(value) => onSettingChange(setting.id, value)}
                />
              </div>
            ))}
          </div>
        </div>
      ))}
    </div>
  );
}
