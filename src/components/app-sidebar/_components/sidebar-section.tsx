import type { ComponentType } from "react";

import { AccordionTrigger } from "@/components/ui/accordion";

export type RenderSettingChange<Settings> = <Key extends keyof Settings>(
  key: Key,
  value: Settings[Key],
) => void;

export function SidebarSectionTrigger({
  icon: Icon,
  title,
  value,
}: {
  icon: ComponentType<{ className?: string }>;
  title: string;
  value: string;
}) {
  return (
    <AccordionTrigger className="min-h-9 gap-2 rounded-md px-2 py-1.5 text-sidebar-foreground hover:bg-sidebar-accent hover:no-underline">
      <span className="flex min-w-0 flex-1 items-center gap-2">
        <Icon className="size-3.5 shrink-0 text-sidebar-foreground/55" />
        <span className="min-w-0 flex-1 truncate text-[13px]">{title}</span>
        <span className="max-w-34 truncate text-[11px] font-normal text-muted-foreground">
          {value}
        </span>
      </span>
    </AccordionTrigger>
  );
}

export function SidebarSectionMeta({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex items-center justify-between gap-2">
      <span className="text-[10px] font-medium tracking-wide text-muted-foreground uppercase">
        {label}
      </span>
      <span className="text-[10px] text-muted-foreground">{value}</span>
    </div>
  );
}
