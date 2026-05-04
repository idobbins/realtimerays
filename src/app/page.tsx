import { AppSidebar } from "@/components/app-sidebar";
import { RenderScene } from "@/components/render-scene";
import { SidebarInset, SidebarProvider } from "@/components/ui/sidebar";

export default function Home() {
  return (
    <SidebarProvider
      style={
        {
          "--sidebar-width": "24rem",
        } as React.CSSProperties
      }
    >
      <AppSidebar />
      <SidebarInset className="bg-muted/60 p-2 pl-0 md:p-3 md:pl-0">
        <div className="flex min-h-[calc(100svh-1rem)] w-full flex-col overflow-hidden rounded-xl bg-background shadow-sm ring-1 ring-border/70 md:min-h-[calc(100svh-1.5rem)]">
          <section className="min-h-0 flex-1">
            <RenderScene />
          </section>
        </div>
      </SidebarInset>
    </SidebarProvider>
  );
}
