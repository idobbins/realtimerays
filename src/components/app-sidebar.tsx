import {
  ApertureIcon,
  BoxIcon,
  ChevronDownIcon,
  CircleHelpIcon,
  Disc3Icon,
  EllipsisIcon,
  LogOutIcon,
  SlidersHorizontalIcon,
  UserPlusIcon,
} from "lucide-react";

import { Avatar, AvatarFallback } from "@/components/ui/avatar";
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuGroup,
  DropdownMenuItem,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu";
import {
  Sidebar,
  SidebarContent,
  SidebarGroup,
  SidebarGroupContent,
  SidebarHeader,
  SidebarMenu,
  SidebarMenuButton,
  SidebarMenuItem,
} from "@/components/ui/sidebar";

const navItems = [
  {
    title: "Render",
    icon: ApertureIcon,
    active: true,
  },
  {
    title: "Scenes",
    icon: BoxIcon,
  },
  {
    title: "Materials",
    icon: Disc3Icon,
  },
  {
    title: "Settings",
    icon: SlidersHorizontalIcon,
  },
];

export function AppSidebar() {
  return (
    <Sidebar collapsible="none" className="hidden h-svh border-r-0 bg-muted/60 md:flex">
      <SidebarHeader className="gap-3 px-3 pt-4 pb-3">
        <SidebarMenu>
          <SidebarMenuItem>
            <DropdownMenu>
              <DropdownMenuTrigger
                render={
                  <SidebarMenuButton
                    size="lg"
                    className="h-8 gap-2 rounded-md px-1.5 data-open:bg-sidebar-accent data-open:text-sidebar-accent-foreground"
                  />
                }
              >
                <Avatar size="sm">
                  <AvatarFallback>ID</AvatarFallback>
                </Avatar>
                <span className="truncate text-sm font-medium">idobbins</span>
                <ChevronDownIcon className="ml-auto size-3.5 text-sidebar-foreground/50" />
              </DropdownMenuTrigger>
              <DropdownMenuContent side="bottom" align="start" className="w-48 p-1.5">
                <DropdownMenuGroup>
                  <DropdownMenuItem>
                    <SlidersHorizontalIcon />
                    <span>Settings</span>
                  </DropdownMenuItem>
                  <DropdownMenuItem>
                    <UserPlusIcon />
                    <span>Invite people</span>
                  </DropdownMenuItem>
                </DropdownMenuGroup>
                <DropdownMenuSeparator />
                <DropdownMenuGroup>
                  <DropdownMenuItem variant="destructive">
                    <LogOutIcon />
                    <span>Log out</span>
                  </DropdownMenuItem>
                </DropdownMenuGroup>
              </DropdownMenuContent>
            </DropdownMenu>
          </SidebarMenuItem>
        </SidebarMenu>
      </SidebarHeader>
      <SidebarContent>
        <SidebarGroup className="p-3 pt-1">
          <SidebarGroupContent>
            <SidebarMenu>
              {navItems.map((item) => (
                <SidebarMenuItem key={item.title}>
                  <SidebarMenuButton
                    isActive={item.active}
                    tooltip={item.title}
                    className="h-7 rounded-md px-2 text-[13px] text-sidebar-foreground/80 group-data-[collapsible=icon]:justify-center group-data-[collapsible=icon]:p-0!"
                  >
                    <item.icon className="text-sidebar-foreground/65" />
                    <span className="group-data-[collapsible=icon]:hidden">{item.title}</span>
                  </SidebarMenuButton>
                </SidebarMenuItem>
              ))}
            </SidebarMenu>
          </SidebarGroupContent>
        </SidebarGroup>
        <SidebarGroup className="p-3 pt-4 group-data-[collapsible=icon]:hidden">
          <div className="mb-1 flex items-center justify-between px-2 text-xs text-muted-foreground">
            <span>Workspace</span>
            <EllipsisIcon className="size-3.5" />
          </div>
          <SidebarGroupContent>
            <SidebarMenu>
              <SidebarMenuItem>
                <SidebarMenuButton className="h-7 rounded-md px-2 text-[13px] text-sidebar-foreground/80">
                  <CircleHelpIcon className="text-sidebar-foreground/65" />
                  <span>Notes</span>
                </SidebarMenuButton>
              </SidebarMenuItem>
            </SidebarMenu>
          </SidebarGroupContent>
        </SidebarGroup>
      </SidebarContent>
    </Sidebar>
  );
}
