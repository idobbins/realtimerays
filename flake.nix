{
  description = "CMake-based build and development shell for realtimerays";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  };

  outputs = { self, nixpkgs, ... }:
    let
      systems = [
        "aarch64-darwin"
        "x86_64-darwin"
      ];

      forAllSystems = nixpkgs.lib.genAttrs systems;

      perSystem = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          lib = pkgs.lib;

          sdkRoot = pkgs.apple-sdk.sdkroot;
          moltenvkIcd = "${pkgs.moltenvk}/share/vulkan/icd.d/MoltenVK_icd.json";
          vulkanRuntimeLibPath = lib.makeLibraryPath [
            pkgs.vulkan-loader
            pkgs.moltenvk
          ];

          vulkanSdk = pkgs.symlinkJoin {
            name = "realtimerays-vulkan-sdk";
            paths = [
              pkgs.shaderc.bin
              pkgs.vulkan-headers
              pkgs.vulkan-loader
            ];
          };

          cmakeCommonFlags = [
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
            "-DCMAKE_OSX_SYSROOT=${sdkRoot}"
            "-DVulkan_INCLUDE_DIR=${pkgs.vulkan-headers}/include"
            "-DVulkan_LIBRARY=${pkgs.vulkan-loader}/lib/libvulkan.dylib"
            "-DVulkan_GLSLC_EXECUTABLE=${pkgs.shaderc.bin}/bin/glslc"
          ];

          cmakeLocalFlags = [
            "-DCMAKE_C_COMPILER=${pkgs.stdenv.cc}/bin/cc"
            "-DCMAKE_OBJC_COMPILER=${pkgs.stdenv.cc}/bin/cc"
          ] ++ cmakeCommonFlags;

          configureLocal = pkgs.writeShellApplication {
            name = "realtimerays-configure";
            runtimeInputs = [
              pkgs.cmake
              pkgs.ninja
            ];
            text = ''
              root="''${REALTIMERAYS_ROOT:-$PWD}"
              build_dir="''${REALTIMERAYS_BUILD_DIR:-$root/build}"

              export SDKROOT=${sdkRoot}
              export VULKAN_SDK=${vulkanSdk}
              export VK_ICD_FILENAMES="''${VK_ICD_FILENAMES:-${moltenvkIcd}}"
              export DYLD_LIBRARY_PATH="${vulkanRuntimeLibPath}:''${DYLD_LIBRARY_PATH:-}"

              cmake -S "$root" -B "$build_dir" -G Ninja \
                -DCMAKE_BUILD_TYPE="''${CMAKE_BUILD_TYPE:-Debug}" \
                ${lib.concatStringsSep " \\\n                " cmakeLocalFlags} \
                "$@"

              if [ -f "$build_dir/compile_commands.json" ]; then
                ln -sf "$build_dir/compile_commands.json" "$root/compile_commands.json"
              fi
            '';
          };

          buildLocal = pkgs.writeShellApplication {
            name = "realtimerays-build";
            runtimeInputs = [
              configureLocal
              pkgs.cmake
              pkgs.ninja
            ];
            text = ''
              root="''${REALTIMERAYS_ROOT:-$PWD}"
              build_dir="''${REALTIMERAYS_BUILD_DIR:-$root/build}"

              if [ ! -f "$build_dir/CMakeCache.txt" ]; then
                realtimerays-configure
              fi

              cmake --build "$build_dir" --parallel "$@"

              if [ -f "$build_dir/compile_commands.json" ]; then
                ln -sf "$build_dir/compile_commands.json" "$root/compile_commands.json"
              fi

              echo "built $build_dir/greatbadbeyond"
            '';
          };

          runLocal = pkgs.writeShellApplication {
            name = "realtimerays-run";
            runtimeInputs = [ buildLocal ];
            text = ''
              root="''${REALTIMERAYS_ROOT:-$PWD}"
              build_dir="''${REALTIMERAYS_BUILD_DIR:-$root/build}"

              realtimerays-build

              export VK_ICD_FILENAMES="''${VK_ICD_FILENAMES:-${moltenvkIcd}}"
              export DYLD_LIBRARY_PATH="${vulkanRuntimeLibPath}:''${DYLD_LIBRARY_PATH:-}"
              exec "$build_dir/greatbadbeyond" "$@"
            '';
          };
        in
        {
          packages = {
            default = pkgs.stdenv.mkDerivation {
              pname = "realtimerays";
              version = "0.1.0";
              src = ./.;

              nativeBuildInputs = [
                pkgs.cmake
                pkgs.ninja
                pkgs.shaderc.bin
              ];

              buildInputs = [
                pkgs.vulkan-headers
                pkgs.vulkan-loader
                pkgs.moltenvk
                pkgs.apple-sdk
              ];

              cmakeFlags = [
                "-DCMAKE_BUILD_TYPE=Release"
              ] ++ cmakeCommonFlags;

              installPhase = ''
                                runHook preInstall

                                mkdir -p "$out/bin" "$out/libexec"

                                exe=greatbadbeyond
                                if [ ! -f "$exe" ]; then
                                  exe=build/greatbadbeyond
                                fi
                                cp "$exe" "$out/libexec/greatbadbeyond"

                                cat > "$out/bin/greatbadbeyond" <<EOF
                #!/bin/sh
                export VK_ICD_FILENAMES="\''${VK_ICD_FILENAMES:-${moltenvkIcd}}"
                export DYLD_LIBRARY_PATH="${vulkanRuntimeLibPath}:\''${DYLD_LIBRARY_PATH:-}"
                exec "$out/libexec/greatbadbeyond" "\$@"
                EOF
                                chmod +x "$out/bin/greatbadbeyond"

                                runHook postInstall
              '';

              meta = {
                description = "GLFW-free Vulkan/MoltenVK realtime rays experiment";
                mainProgram = "greatbadbeyond";
                platforms = lib.platforms.darwin;
              };
            };

            configure = configureLocal;
            build-local = buildLocal;
            run-local = runLocal;
          };

          apps = {
            default = {
              type = "app";
              program = "${self.packages.${system}.default}/bin/greatbadbeyond";
            };
            configure = {
              type = "app";
              program = "${configureLocal}/bin/realtimerays-configure";
            };
            build = {
              type = "app";
              program = "${buildLocal}/bin/realtimerays-build";
            };
            run-local = {
              type = "app";
              program = "${runLocal}/bin/realtimerays-run";
            };
          };

          devShells.default = pkgs.mkShell {
            packages = [
              configureLocal
              buildLocal
              runLocal
              pkgs.cmake
              pkgs.ninja
              pkgs.pkg-config
              pkgs.shaderc.bin
              pkgs.vulkan-headers
              pkgs.vulkan-loader
              pkgs.vulkan-tools
              pkgs.vulkan-validation-layers
              pkgs.moltenvk
              pkgs.apple-sdk
              pkgs.zsh
            ];

            shellHook = ''
                            export REALTIMERAYS_ROOT="$PWD"
                            export VULKAN_SDK=${vulkanSdk}
                            export SDKROOT=${sdkRoot}
                            export VK_LAYER_PATH=${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d
                            export VK_ICD_FILENAMES="''${VK_ICD_FILENAMES:-${moltenvkIcd}}"
                            export DYLD_LIBRARY_PATH="${vulkanRuntimeLibPath}:''${DYLD_LIBRARY_PATH:-}"

                            if [ ! -f "$PWD/build/compile_commands.json" ]; then
                              realtimerays-configure >/dev/null || true
                            elif [ ! -e "$PWD/compile_commands.json" ]; then
                              ln -sf "$PWD/build/compile_commands.json" "$PWD/compile_commands.json"
                            fi

                            export ZDOTDIR="$PWD/.nix-zsh"
                            mkdir -p "$ZDOTDIR"
                            cat > "$ZDOTDIR/.zshrc" <<'EOF'
              # Generated by flake.nix. Edit flake.nix, not this file.
              if [[ -r "$HOME/.zshrc" && -z "$REALTIMERAYS_NO_USER_ZSHRC" ]]; then
                source "$HOME/.zshrc"
              fi

              alias configure='realtimerays-configure'
              alias build='realtimerays-build'
              alias run='realtimerays-run'
              alias clean-build='realtimerays clean-build'
              alias nix-build='nix build path:$PWD'
              alias nix-run='nix run path:$PWD'

              realtimerays() {
                local cmd="''${1:-}"
                case "$cmd" in
                  configure) shift; realtimerays-configure "$@" ;;
                  build) shift; realtimerays-build "$@" ;;
                  run) shift; realtimerays-run "$@" ;;
                  clean-build) shift; rm -rf "''${REALTIMERAYS_BUILD_DIR:-''${REALTIMERAYS_ROOT:-$PWD}/build}" && realtimerays-build "$@" ;;
                  nix-build) shift; nix build path:$PWD "$@" ;;
                  nix-run) shift; nix run path:$PWD "$@" ;;
                  ""|-h|--help|help)
                    print 'usage: realtimerays <command>'
                    print 'commands: configure build run clean-build nix-build nix-run'
                    ;;
                  *)
                    print -u2 "realtimerays: unknown command: $cmd"
                    return 2
                    ;;
                esac
              }
              EOF

                            export SHELL=${pkgs.zsh}/bin/zsh
                            case "$-" in
                              *i*)
                                if [ -z "''${REALTIMERAYS_IN_ZSH:-}" ]; then
                                  export REALTIMERAYS_IN_ZSH=1
                                  exec ${pkgs.zsh}/bin/zsh -i
                                fi
                                ;;
                            esac
            '';
          };
        });
    in
    {
      packages = forAllSystems (system: perSystem.${system}.packages);
      apps = forAllSystems (system: perSystem.${system}.apps);
      devShells = forAllSystems (system: perSystem.${system}.devShells);
      formatter = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; };
        in pkgs.nixpkgs-fmt);
    };
}
