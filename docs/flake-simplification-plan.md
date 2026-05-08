# Flake Simplification Plan

## Goal

Make `flake.nix` responsible for Nix-specific concerns only:

- pinning/providing dependencies;
- exposing a package, app, and development shell;
- setting unavoidable macOS/MoltenVK runtime environment.

Move build-system responsibilities back to CMake:

- configure/build/install behavior;
- executable installation;
- build directory conventions/presets;
- Vulkan and `glslc` discovery through normal CMake mechanisms.

## Investigation Summary

`flake.nix` currently duplicates or manually orchestrates a lot of work CMake already handles:

1. **CMake cache flags duplicated in Nix**
   - `CMAKE_EXPORT_COMPILE_COMMANDS` is set both in `CMakeLists.txt` and in `flake.nix`.
   - `CMAKE_C_COMPILER` / `CMAKE_OBJC_COMPILER` are forced in the local wrapper, even though Nix's stdenv compiler and CMake setup hook should supply them.
   - `CMAKE_OSX_SYSROOT` is forced from Nix, although Darwin stdenv/apple SDK setup should normally provide the SDK.
   - `Vulkan_INCLUDE_DIR`, `Vulkan_LIBRARY`, and `Vulkan_GLSLC_EXECUTABLE` are forced, although `CMakeLists.txt` already uses `find_package(Vulkan REQUIRED)` and falls back to `find_program(glslc)`.

2. **Local configure/build/run scripts reimplement CMake workflows**
   - `realtimerays-configure` wraps `cmake -S/-B`.
   - `realtimerays-build` conditionally configures then calls `cmake --build`.
   - `realtimerays-run` builds then executes the binary.
   - This is mostly equivalent to CMake presets plus a small documented command sequence.

3. **Package install is manual because CMake has no install rule**
   - The derivation copies `greatbadbeyond` by hand in `installPhase`.
   - Adding `install(TARGETS greatbadbeyond RUNTIME DESTINATION bin)` to `CMakeLists.txt` would let the Nix CMake builder use the normal install phase.

4. **Shader generated path is hard-coded to the source tree's `build/`**
   - `CMakeLists.txt` uses `${CMAKE_SOURCE_DIR}/build/generated/shaders`.
   - This couples CMake to a specific local build directory and undermines out-of-source/Nix builds.
   - Prefer `${CMAKE_CURRENT_BINARY_DIR}/generated/shaders`.

5. **Editor and shell customization is mixed into the package flake**
   - `syncZedConfig` writes `.zed` files from inside `flake.nix`.
   - `shellHook` generates a zsh config and aliases/functions.
   - These are convenience features, but they make the flake much larger than the build definition.

## Proposed Target Shape

### CMake owns build/install conventions

Add/adjust CMake configuration:

- Use the binary directory for generated shaders:
  - `set(SHADER_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders)`
- Add an install rule:
  - `install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)`
- Optionally add `CMakePresets.json` for local development:
  - configure preset: Ninja, `build`, Debug, compile commands on;
  - build preset: references the configure preset.

Example local commands after this change:

```sh
nix develop
cmake --preset dev
cmake --build --preset dev
./build/greatbadbeyond
```

### Nix package uses the standard CMake builder

Simplify `packages.default` to roughly:

```nix
pkgs.stdenv.mkDerivation {
  pname = "realtimerays";
  version = "0.1.0";
  src = ./.;

  nativeBuildInputs = [
    pkgs.cmake
    pkgs.ninja
    pkgs.shaderc.bin
    pkgs.makeWrapper
  ];

  buildInputs = [
    pkgs.vulkan-headers
    pkgs.vulkan-loader
    pkgs.moltenvk
    pkgs.apple-sdk
  ];

  cmakeBuildType = "Release";

  postInstall = ''
    wrapProgram "$out/bin/greatbadbeyond" \
      --set-default VK_ICD_FILENAMES ${moltenvkIcd} \
      --prefix DYLD_LIBRARY_PATH : ${vulkanRuntimeLibPath}
  '';

  meta.mainProgram = "greatbadbeyond";
}
```

Notes:

- Try without explicit `cmakeFlags` first.
- Keep only flags that prove necessary after validation.
- Runtime wrapping remains Nix-specific because `VK_ICD_FILENAMES` and library paths refer to Nix store paths.

### Dev shell provides tools/environment, not build orchestration

Simplify `devShells.default` to provide tools plus runtime env:

- `inputsFrom = [ self.packages.${system}.default ];` or a shared dependency list;
- extra developer tools: `vulkan-tools`, `vulkan-validation-layers`, maybe `zsh`;
- `shellHook` limited to environment variables:
  - `VULKAN_SDK` if CMake or tooling still benefits from it;
  - `VK_ICD_FILENAMES` for MoltenVK;
  - `VK_LAYER_PATH` for validation layers;
  - `DYLD_LIBRARY_PATH` only if running the local unwrapped build needs it.

Avoid auto-configuring the project from `shellHook`; entering a shell should not mutate the worktree unless explicitly requested.

### Editor configuration should be static or separate

Preferred options:

1. Commit simple `.zed/*.json` files that call standard commands, e.g.:
   - `nix develop --command cmake --preset dev`
   - `nix develop --command cmake --build --preset dev`
   - `nix develop --command sh -c 'cmake --build --preset dev && ./build/greatbadbeyond'`
2. Or keep a small manually-run helper outside the main flake path if generation is still desired.

Recommendation: remove `syncZedConfig` from `flake.nix` and keep editor config as ordinary project files.

## Phased Implementation Plan

### Phase 1: Make CMake self-contained

1. Change `SHADER_GENERATED_DIR` to use `CMAKE_CURRENT_BINARY_DIR`.
2. Add `install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)`.
3. Add `CMakePresets.json` for local configure/build commands.
4. Validate local CMake inside `nix develop`:

```sh
rm -rf build compile_commands.json
nix develop --command cmake --preset dev
nix develop --command cmake --build --preset dev
nix develop --command ./build/greatbadbeyond
```

### Phase 2: Simplify the Nix package

1. Remove custom `configureLocal`, `buildLocal`, and `runLocal` packages/apps.
2. Remove `cmakeCommonFlags` / `cmakeLocalFlags` unless validation proves a specific flag is required.
3. Use the standard CMake derivation phases.
4. Replace manual `installPhase` with `postInstall` wrapping only.
5. Validate:

```sh
nix build
nix run
```

### Phase 3: Simplify the development shell

1. Keep dependencies and runtime environment only.
2. Remove auto-configure and compile command symlink logic from `shellHook`.
3. If desired, document a manual symlink command or let editors read `build/compile_commands.json` directly.
4. Validate:

```sh
nix develop
cmake --preset dev
cmake --build --preset dev
```

### Phase 4: Decouple editor config

1. Stop generating `.zed` files from `flake.nix`.
2. Update committed `.zed/tasks.json` to use CMake presets and `nix develop` directly.
3. Remove `sync-zed-config` package/app if no longer needed.

## Expected Result

`flake.nix` should shrink to mostly:

- input pinning;
- per-system package definition;
- default app pointing at the wrapped installed executable;
- dev shell with dependencies and runtime env;
- formatter.

CMake should become the single source of truth for configuring, building, generated files, and installing the executable.

## Validation Checklist

- `nix flake show` still exposes `packages.default`, `apps.default`, `devShells.default`, and `formatter`.
- `nix build` succeeds from a clean checkout.
- `nix run` launches the wrapped app with MoltenVK available.
- `nix develop --command cmake --preset dev` succeeds.
- `nix develop --command cmake --build --preset dev` succeeds.
- Local executable can run from the dev shell.
- Zed build/run/debug tasks still work after updating them to the new commands.
