import { InfoIcon } from "lucide-react";

import { Tooltip, TooltipContent, TooltipTrigger } from "@/components/ui/tooltip";

export function CameraModeInfo({ description }: { description: string }) {
  return (
    <Tooltip>
      <TooltipTrigger
        render={
          <span className="inline-flex size-4 shrink-0 items-center justify-center rounded-sm text-muted-foreground transition-colors hover:text-foreground" />
        }
      >
        <InfoIcon className="size-3" aria-hidden="true" />
      </TooltipTrigger>
      <TooltipContent side="right" align="center">
        {description}
      </TooltipContent>
    </Tooltip>
  );
}
