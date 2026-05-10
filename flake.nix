{
  description = "Rust/WGPU realtime rays experiment";

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
        in
        {
          default = pkgs.rustPlatform.buildRustPackage {
            pname = "realtimerays";
            version = "0.1.0";
            src = ./.;

            cargoLock.lockFile = ./Cargo.lock;

            buildInputs = [
              pkgs.apple-sdk
            ];

            meta = {
              description = "WGPU/Metal realtime rays experiment";
              mainProgram = "realtimerays";
              platforms = lib.platforms.darwin;
            };
          };
        });

      apps = forAllSystems (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/realtimerays";
        };
      });

      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          devCommands = [
            (pkgs.writeShellApplication {
              name = "build";
              runtimeInputs = [ pkgs.cargo ];
              text = ''
                exec cargo build "$@"
              '';
            })
            (pkgs.writeShellApplication {
              name = "run";
              runtimeInputs = [ pkgs.cargo ];
              text = ''
                exec cargo run "$@"
              '';
            })
            (pkgs.writeShellApplication {
              name = "clean-build";
              runtimeInputs = [ pkgs.cargo ];
              text = ''
                rm -rf target
                exec cargo build "$@"
              '';
            })
          ];
        in
        {
          default = pkgs.mkShell {
            buildInputs = [
              pkgs.apple-sdk
            ];

            packages = [
              pkgs.cargo
              pkgs.clippy
              pkgs.pkg-config
              pkgs.rustc
              pkgs.rustfmt
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
