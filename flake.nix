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
    in
    {
      packages = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          lib = pkgs.lib;

          moltenvkIcd = "${pkgs.moltenvk}/share/vulkan/icd.d/MoltenVK_icd.json";
          vulkanRuntimeLibPath = lib.makeLibraryPath [
            pkgs.vulkan-loader
            pkgs.moltenvk
          ];
        in
        {
          default = pkgs.stdenv.mkDerivation {
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

            meta = {
              description = "GLFW-free Vulkan/MoltenVK realtime rays experiment";
              mainProgram = "greatbadbeyond";
              platforms = lib.platforms.darwin;
            };
          };
        });

      apps = forAllSystems (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/greatbadbeyond";
        };
      });

      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          lib = pkgs.lib;

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
          devCommands = [
            (pkgs.writeShellApplication {
              name = "configure";
              runtimeInputs = [ pkgs.cmake ];
              text = ''
                exec cmake --preset dev "$@"
              '';
            })
            (pkgs.writeShellApplication {
              name = "build";
              runtimeInputs = [ pkgs.cmake pkgs.ninja ];
              text = ''
                exec cmake --build --preset dev "$@"
              '';
            })
            (pkgs.writeShellApplication {
              name = "run";
              text = ''
                exec ./build/greatbadbeyond "$@"
              '';
            })
            (pkgs.writeShellApplication {
              name = "clean-build";
              runtimeInputs = [ pkgs.cmake pkgs.ninja ];
              text = ''
                rm -rf build compile_commands.json
                cmake --preset dev
                exec cmake --build --preset dev "$@"
              '';
            })
          ];
        in
        {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.default ];

            VULKAN_SDK = vulkanSdk;
            VK_LAYER_PATH = "${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d";
            VK_ICD_FILENAMES = moltenvkIcd;
            DYLD_LIBRARY_PATH = vulkanRuntimeLibPath;

            packages = [
              pkgs.cmake
              pkgs.ninja
              pkgs.pkg-config
              pkgs.shaderc.bin
              pkgs.vulkan-tools
              pkgs.vulkan-validation-layers
              pkgs.zsh
            ] ++ devCommands;

            shellHook = ''
              if [ -z "$REALTIMERAYS_IN_ZSH" ] && [ -z "$DIRENV_DIR" ] && [ -z "$DIRENV_IN_ENVRC" ] && [ -t 0 ]; then
                export REALTIMERAYS_IN_ZSH=1
                exec ${pkgs.zsh}/bin/zsh
              fi
            '';
          };
        });

      formatter = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; };
        in pkgs.nixpkgs-fmt);
    };
}
