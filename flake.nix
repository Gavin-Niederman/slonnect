{
  description = "Slonnect - Material Design KDE Connect clients for the desktop and Android";

  inputs.nixpkgs.url = "nixpkgs";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
      androidPkgs = pkgs: import nixpkgs {
        inherit (pkgs.stdenv.hostPlatform) system;
        config = { allowUnfree = true; android_sdk.accept_license = true; };
      };
    in {
      packages = forAll (pkgs: {
        default = pkgs.qt6Packages.callPackage ./nix/package.nix { };
        slonnect = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
        # Patched kdeconnect-kde (see nix/kdeconnect.nix).
        kdeconnect = pkgs.callPackage ./nix/kdeconnect.nix { };
        # Google Messages for Web helper (desktop/helpers/gmessages, AGPL-3.0).
        gmessages = pkgs.callPackage ./nix/gmessages.nix { };
      });

      overlays.default = final: prev: {
        slonnect = final.qt6Packages.callPackage ./nix/package.nix { };
        slonnect-kdeconnect = final.callPackage ./nix/kdeconnect.nix { };
        slonnect-gmessages = final.callPackage ./nix/gmessages.nix { };
      };

      # Home Manager: `services.slonnect.enable = true;` runs the patched kdeconnectd
      # and installs Slonnect. Replaces `services.kdeconnect`.
      homeManagerModules.default = import ./nix/hm-module.nix self;
      homeModules.default = self.homeManagerModules.default;

      devShells = forAll (pkgs: {
        # Android app (mobile/). The SDK is unfree, so these import nixpkgs with the
        # licence accepted instead of using `pkgs`.
        android = (androidPkgs pkgs).callPackage ./nix/android-shell.nix { };
        android-emulator = (androidPkgs pkgs).callPackage ./nix/android-shell.nix { withEmulator = true; };

        default = pkgs.mkShell {
          inputsFrom = [ self.packages.${pkgs.stdenv.hostPlatform.system}.default ];
          packages = [ pkgs.qt6.qttools pkgs.gdb pkgs.scrcpy pkgs.android-tools pkgs.go pkgs.just ];
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
