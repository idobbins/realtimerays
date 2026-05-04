# RealtimeRays

A modular real-time path tracing playground for the browser.

## Tech stack

- Next.js 16
- React 19
- TypeScript
- Tailwind CSS 4
- Three.js
- Base UI / shadcn-style components

## Getting started

```bash
pnpm install
pnpm dev
```

Open [http://localhost:3000](http://localhost:3000).

## Scripts

```bash
pnpm dev      # start the local dev server
pnpm build    # create a production build
pnpm start    # start the production server
pnpm lint     # run oxlint with React, a11y, and Next.js rules
pnpm fmt      # format the project with oxfmt
```

## Project structure

```txt
src/app                     Next.js app routes, layout, and global CSS
src/components              App-specific React components
src/components/app-sidebar  Sidebar component with colocated _components
src/components/ui           Reusable UI primitives
src/hooks                   Shared React hooks
src/lib                     Shared utilities, pipeline data, and types
```

## Current state

RealtimeRays currently includes:

- A configurable render-pipeline sidebar
- A Three.js preview scene
- Local UI state for pipeline, camera, renderer, denoiser, display, and debug settings

The sidebar controls are not yet wired into a real path tracing backend.

## Development tools

React Grab is loaded automatically in development only. It is not included in production builds.

## Roadmap

- Connect sidebar settings to renderer state
- Implement a browser path tracing backend
- Add scene loading
- Add screenshot and recording export
- Add persistence/auth/backend integrations if needed
