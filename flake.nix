{
  description = "Slonnect - a Material Design desktop client for KDE Connect";

  inputs.nixpkgs.url = "nixpkgs";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in {
      packages = forAll (pkgs: {
        default = pkgs.qt6Packages.callPackage ./nix/package.nix { };
      });

      devShells = forAll (pkgs: {
        default = pkgs.mkShell {
          inputsFrom = [ self.packages.${pkgs.stdenv.hostPlatform.system}.default ];
          packages = [ pkgs.qt6.qttools pkgs.gdb pkgs.scrcpy pkgs.android-tools ];
          shellHook = ''
            export QML_IMPORT_PATH=${pkgs.qt6.qtdeclarative}/${pkgs.qt6.qtbase.qtQmlPrefix}
            # The packaged app gets plugin paths from wrapQtAppsHook; mirror that for ./build runs
            # (SVG icons need the qtsvg image-format plugin, Wayland needs qtwayland).
            export QT_PLUGIN_PATH=${pkgs.qt6.qtsvg}/${pkgs.qt6.qtbase.qtPluginPrefix}:${pkgs.qt6.qtwayland}/${pkgs.qt6.qtbase.qtPluginPrefix}:${pkgs.qt6.qtbase}/${pkgs.qt6.qtbase.qtPluginPrefix}''${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}
          '';
        };
      });
    };
}
