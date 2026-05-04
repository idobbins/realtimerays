"use client";

import * as React from "react";
import { Progress as ProgressPrimitive } from "@base-ui/react/progress";

import { cn } from "@/lib/utils";

function getProgressScale(value: number | null, min: number, max: number) {
  if (!Number.isFinite(value) || max <= min) return 0;
  return Math.min(1, Math.max(0, ((value as number) - min) / (max - min)));
}

function withProgressScale(style: React.CSSProperties | undefined, scale: string) {
  return {
    ...style,
    "--progress-scale": scale,
  } as React.CSSProperties & { "--progress-scale": string };
}

function Progress({
  className,
  children,
  value,
  min = 0,
  max = 100,
  style,
  ...props
}: ProgressPrimitive.Root.Props) {
  const progressScale = getProgressScale(value, min, max).toString();
  const progressStyle: ProgressPrimitive.Root.Props["style"] =
    typeof style === "function"
      ? (state) => withProgressScale(style(state), progressScale)
      : withProgressScale(style, progressScale);

  return (
    <ProgressPrimitive.Root
      value={value}
      min={min}
      max={max}
      data-slot="progress"
      style={progressStyle}
      className={cn("flex flex-wrap gap-3", className)}
      {...props}
    >
      {children}
      <ProgressTrack>
        <div
          data-slot="progress-indicator"
          className="h-full w-full origin-left scale-x-[var(--progress-scale)] rounded-full bg-primary transition-transform duration-200 ease-in-out will-change-transform motion-reduce:transition-none"
        />
      </ProgressTrack>
    </ProgressPrimitive.Root>
  );
}

function ProgressTrack({ className, ...props }: ProgressPrimitive.Track.Props) {
  return (
    <ProgressPrimitive.Track
      className={cn(
        "relative flex h-1 w-full items-center overflow-x-hidden rounded-full bg-muted",
        className,
      )}
      data-slot="progress-track"
      {...props}
    />
  );
}

function ProgressIndicator({ className, ...props }: ProgressPrimitive.Indicator.Props) {
  return (
    <ProgressPrimitive.Indicator
      data-slot="progress-indicator"
      className={cn(
        "h-full bg-primary transition-[width,background-color] duration-200 ease-in-out motion-reduce:transition-none",
        className,
      )}
      {...props}
    />
  );
}

function ProgressLabel({ className, ...props }: ProgressPrimitive.Label.Props) {
  return (
    <ProgressPrimitive.Label
      className={cn("text-sm font-medium", className)}
      data-slot="progress-label"
      {...props}
    />
  );
}

function ProgressValue({ className, ...props }: ProgressPrimitive.Value.Props) {
  return (
    <ProgressPrimitive.Value
      className={cn("ml-auto text-sm text-muted-foreground tabular-nums", className)}
      data-slot="progress-value"
      {...props}
    />
  );
}

export { Progress, ProgressTrack, ProgressIndicator, ProgressLabel, ProgressValue };
