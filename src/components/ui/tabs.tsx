"use client";

import * as React from "react";
import { Tabs as TabsPrimitive } from "@base-ui/react/tabs";
import { cva, type VariantProps } from "class-variance-authority";
import { motion, useReducedMotion, type HTMLMotionProps } from "motion/react";

import { cn } from "@/lib/utils";

type IndicatorVariant = NonNullable<VariantProps<typeof tabsListVariants>["variant"]>;
type TabsListVariant = IndicatorVariant | null | undefined;
type IndicatorState = TabsPrimitive.Indicator.State;
type IndicatorRenderProps = React.ComponentPropsWithoutRef<"span"> & {
  ref?: React.Ref<HTMLSpanElement>;
};
type IndicatorRect = {
  x: number;
  y: number;
  width: number;
  height: number;
};

const tabIndicatorTransition = {
  type: "spring",
  duration: 0.35,
  bounce: 0.12,
} as const;

const instantIndicatorTransition = {
  duration: 0,
} as const;

function Tabs({ className, orientation = "horizontal", ...props }: TabsPrimitive.Root.Props) {
  return (
    <TabsPrimitive.Root
      data-slot="tabs"
      data-orientation={orientation}
      className={cn("group/tabs flex gap-2 data-horizontal:flex-col", className)}
      {...props}
    />
  );
}

const tabsListVariants = cva(
  "group/tabs-list relative isolate inline-flex w-fit items-center justify-center rounded-lg p-[3px] text-muted-foreground group-data-horizontal/tabs:h-8 group-data-vertical/tabs:h-fit group-data-vertical/tabs:flex-col data-[variant=line]:rounded-none",
  {
    variants: {
      variant: {
        default: "bg-muted",
        chrome:
          "gap-1 rounded-none bg-transparent p-0 text-muted-foreground group-data-horizontal/tabs:h-8 group-data-vertical/tabs:w-full",
        line: "gap-1 bg-transparent",
      },
    },
    defaultVariants: {
      variant: "default",
    },
  },
);

function TabsList({
  className,
  variant = "default",
  children,
  ...props
}: TabsPrimitive.List.Props & VariantProps<typeof tabsListVariants>) {
  const indicatorVariant = getTabsListVariant(variant);

  return (
    <TabsPrimitive.List
      data-slot="tabs-list"
      data-variant={indicatorVariant}
      className={cn(tabsListVariants({ variant }), className)}
      {...props}
    >
      <TabsIndicator variant={indicatorVariant} />
      {children}
    </TabsPrimitive.List>
  );
}

function TabsTrigger({ className, ...props }: TabsPrimitive.Tab.Props) {
  return (
    <TabsPrimitive.Tab
      data-slot="tabs-trigger"
      className={cn(
        "relative z-10 inline-flex h-[calc(100%-1px)] flex-1 items-center justify-center gap-1.5 rounded-md border border-transparent px-1.5 py-0.5 text-sm font-medium whitespace-nowrap text-foreground/60 transition-[color,border-color,box-shadow] duration-150 ease group-data-vertical/tabs:w-full group-data-vertical/tabs:justify-start hover:text-foreground focus-visible:border-ring focus-visible:ring-[3px] focus-visible:ring-ring/50 focus-visible:outline-1 focus-visible:outline-ring motion-reduce:transition-none disabled:pointer-events-none disabled:opacity-50 has-data-[icon=inline-end]:pr-1 has-data-[icon=inline-start]:pl-1 aria-disabled:pointer-events-none aria-disabled:opacity-50 dark:text-muted-foreground dark:hover:text-foreground [&_svg]:pointer-events-none [&_svg]:shrink-0 [&_svg:not([class*='size-'])]:size-4",
        "group-data-[variant=chrome]/tabs-list:h-8 group-data-[variant=chrome]/tabs-list:w-36 group-data-[variant=chrome]/tabs-list:flex-none group-data-[variant=chrome]/tabs-list:justify-start group-data-[variant=chrome]/tabs-list:rounded-lg group-data-[variant=chrome]/tabs-list:px-3 group-data-[variant=chrome]/tabs-list:text-xs group-data-[variant=chrome]/tabs-list:font-medium group-data-[variant=chrome]/tabs-list:text-muted-foreground group-data-[variant=chrome]/tabs-list:hover:text-foreground",
        "group-data-[variant=chrome]/tabs-list:data-active:text-foreground",
        "group-data-[variant=line]/tabs-list:bg-transparent group-data-[variant=line]/tabs-list:data-active:bg-transparent dark:group-data-[variant=line]/tabs-list:data-active:border-transparent dark:group-data-[variant=line]/tabs-list:data-active:bg-transparent",
        "data-active:text-foreground dark:data-active:text-foreground",
        className,
      )}
      {...props}
    />
  );
}

function TabsIndicator({ variant = "default" }: { variant?: IndicatorVariant }) {
  return (
    <TabsPrimitive.Indicator
      render={(indicatorProps, state) => (
        <TabsSpringIndicator
          indicatorProps={indicatorProps as IndicatorRenderProps}
          state={state}
          variant={variant}
        />
      )}
    />
  );
}

function TabsSpringIndicator({
  indicatorProps,
  state,
  variant,
}: {
  indicatorProps: IndicatorRenderProps;
  state: IndicatorState;
  variant: IndicatorVariant;
}) {
  const { className, ref, style, ...props } = indicatorProps;
  const position = state.activeTabPosition;
  const size = state.activeTabSize;
  const target = React.useMemo(
    () => getIndicatorTarget(position, size, state.orientation, variant),
    [position, size, state.orientation, variant],
  );
  const shouldReduceMotion = useReducedMotion();

  return (
    <motion.span
      {...(props as HTMLMotionProps<"span">)}
      ref={ref}
      className={cn(
        "pointer-events-none absolute top-0 left-0 z-0 will-change-transform",
        variant === "chrome" &&
          "rounded-lg border border-border/70 bg-background shadow-[0_0.5px_1px_rgba(0,0,0,0.035)] dark:bg-input/40",
        variant === "default" &&
          "rounded-md bg-background shadow-sm dark:border dark:border-input dark:bg-input/30",
        variant === "line" && "rounded-full bg-foreground",
        className,
      )}
      initial={false}
      animate={{
        x: target?.x ?? 0,
        y: target?.y ?? 0,
        width: target?.width ?? 0,
        height: target?.height ?? 0,
      }}
      transition={shouldReduceMotion ? instantIndicatorTransition : tabIndicatorTransition}
      style={style}
    />
  );
}

function TabsContent({ className, ...props }: TabsPrimitive.Panel.Props) {
  return (
    <TabsPrimitive.Panel
      data-slot="tabs-content"
      className={cn("flex-1 text-sm outline-none", className)}
      {...props}
    />
  );
}

function getIndicatorTarget(
  position: IndicatorState["activeTabPosition"],
  size: IndicatorState["activeTabSize"],
  orientation: IndicatorState["orientation"],
  variant: IndicatorVariant,
): IndicatorRect | null {
  if (!position || !size) return null;

  if (variant === "line") {
    if (orientation === "vertical") {
      return {
        x: position.left + size.width + 3,
        y: position.top,
        width: 2,
        height: size.height,
      };
    }

    return {
      x: position.left,
      y: position.top + size.height + 3,
      width: size.width,
      height: 2,
    };
  }

  return {
    x: position.left,
    y: position.top,
    width: size.width,
    height: size.height,
  };
}

function getTabsListVariant(variant: TabsListVariant): IndicatorVariant {
  return variant ?? "default";
}

export { Tabs, TabsList, TabsTrigger, TabsContent, TabsIndicator, tabsListVariants };
