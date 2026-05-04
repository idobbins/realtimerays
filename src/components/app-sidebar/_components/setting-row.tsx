import type { ReactNode } from "react";

export function SettingRow({ label, children }: { label: string; children: ReactNode }) {
  return (
    <div className="flex min-h-8 items-center justify-between gap-3 rounded-md px-2 py-1 text-[12px] text-sidebar-foreground/80 transition-colors hover:bg-sidebar-accent/60">
      <span className="min-w-0 flex-1 truncate" title={label}>
        {label}
      </span>
      {children}
    </div>
  );
}

export function Readout({ children }: { children: ReactNode }) {
  return (
    <span className="max-w-34 truncate text-right text-[11px] text-muted-foreground">
      {children}
    </span>
  );
}
