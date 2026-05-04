import { cn } from "@/lib/utils";
import { Loader2Icon } from "lucide-react";

function Spinner({ className, ...props }: React.ComponentProps<"svg">) {
  return (
    <Loader2Icon
      data-slot="spinner"
      role="status"
      aria-label="Loading"
      className={cn(
        "size-4 animate-spin [animation-duration:850ms] will-change-transform motion-reduce:animate-none",
        className,
      )}
      {...props}
    />
  );
}

export { Spinner };
