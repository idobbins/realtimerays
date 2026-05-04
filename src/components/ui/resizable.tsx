"use client";

import * as ResizablePrimitive from "react-resizable-panels";

import { cn } from "@/lib/utils";

function ResizablePanelGroup({ className, ...props }: ResizablePrimitive.GroupProps) {
  return (
    <ResizablePrimitive.Group
      data-slot="resizable-panel-group"
      className={cn("flex h-full w-full aria-[orientation=vertical]:flex-col", className)}
      {...props}
    />
  );
}

function ResizablePanel({ ...props }: ResizablePrimitive.PanelProps) {
  return <ResizablePrimitive.Panel data-slot="resizable-panel" {...props} />;
}

function ResizableHandle({
  withHandle,
  className,
  ...props
}: ResizablePrimitive.SeparatorProps & {
  withHandle?: boolean;
}) {
  return (
    <ResizablePrimitive.Separator
      data-slot="resizable-handle"
      className={cn(
        "group/resizable-handle relative flex w-px items-center justify-center bg-border ring-offset-background transition-[background-color,box-shadow] duration-150 ease after:absolute after:inset-y-0 after:left-1/2 after:w-1 after:-translate-x-1/2 hover:bg-primary/50 focus-visible:bg-primary focus-visible:ring-1 focus-visible:ring-ring focus-visible:outline-hidden active:bg-primary motion-reduce:transition-none aria-[orientation=horizontal]:h-px aria-[orientation=horizontal]:w-full aria-[orientation=horizontal]:after:left-0 aria-[orientation=horizontal]:after:h-1 aria-[orientation=horizontal]:after:w-full aria-[orientation=horizontal]:after:translate-x-0 aria-[orientation=horizontal]:after:-translate-y-1/2 [&[aria-orientation=horizontal]>div]:rotate-90",
        className,
      )}
      {...props}
    >
      {withHandle && (
        <div className="z-10 flex h-6 w-1 shrink-0 rounded-lg bg-border transition-[background-color,transform] duration-150 ease motion-reduce:transition-none group-hover/resizable-handle:bg-primary/70 group-focus-visible/resizable-handle:bg-primary group-active/resizable-handle:scale-x-125 group-active/resizable-handle:bg-primary" />
      )}
    </ResizablePrimitive.Separator>
  );
}

export { ResizableHandle, ResizablePanel, ResizablePanelGroup };
