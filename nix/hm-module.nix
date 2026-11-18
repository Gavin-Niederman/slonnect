# Home Manager module: Slonnect plus the (patched) KDE Connect daemon it talks to.
#
#   imports = [ slonnect.homeManagerModules.default ];
#   services.slonnect.enable = true;
#
# This takes the place of `services.kdeconnect`: it runs kdeconnectd as the same
# graphical-session user service that module does, using the patched kdeconnect-kde
# from this flake, and installs Slonnect as the app for it. KDE Connect's own app and
# SMS client stay installed (and runnable) but are hidden from launchers.
#
# Phones still need to reach this machine on TCP/UDP 1714-1764. On NixOS,
# `programs.kdeconnect.enable` opens those ports, or open them in networking.firewall.
self:
{ config, lib, pkgs, ... }:

let
  cfg = config.services.slonnect;
  system = pkgs.stdenv.hostPlatform.system;

  # Copies of KDE Connect's launchers with NoDisplay set; a desktop file with the same
  # id in ~/.local/share/applications shadows the packaged one.
  hidden = [ "org.kde.kdeconnect.app" "org.kde.kdeconnect.sms" ]
    ++ lib.optional (!cfg.indicator) "org.kde.kdeconnect.nonplasma";
  hiddenEntry = id: {
    name = "applications/${id}.desktop";
    value.source = pkgs.runCommand "${id}-hidden.desktop" { } ''
      sed -e '/^NoDisplay=/d' -e '/^\[Desktop Entry\]/a NoDisplay=true' \
        ${cfg.kdeconnectPackage}/share/applications/${id}.desktop > $out
    '';
  };
in
{
  options.services.slonnect = {
    enable = lib.mkEnableOption "Slonnect, a desktop client for KDE Connect, with the KDE Connect daemon";

    package = lib.mkOption {
      type = lib.types.package;
      default = self.packages.${system}.default;
      defaultText = lib.literalExpression "slonnect.packages.\${system}.default";
      description = "The Slonnect package.";
    };

    kdeconnectPackage = lib.mkOption {
      type = lib.types.package;
      default = self.packages.${system}.kdeconnect;
      defaultText = lib.literalExpression "slonnect.packages.\${system}.kdeconnect";
      description = ''
        kdeconnect-kde providing kdeconnectd. Defaults to this flake's patched build
        (layer-shell presenter pointer); `pkgs.kdePackages.kdeconnect-kde` also works.
      '';
    };

    indicator = lib.mkOption {
      type = lib.types.bool;
      default = false;
      description = "Also run kdeconnect-indicator, KDE Connect's tray icon.";
    };

    hideKdeConnectApps = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = "Hide KDE Connect's own app and SMS client from application launchers.";
    };

    smsHandler = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = ''
        Make Slonnect the handler for sms: and smsto: links. Takes effect when
        `xdg.mimeApps.enable` is set.
      '';
    };
  };

  config = lib.mkIf cfg.enable (lib.mkMerge [
    {
      assertions = [
        (lib.hm.assertions.assertPlatform "services.slonnect" pkgs lib.platforms.linux)
        {
          assertion = !(config.services.kdeconnect.enable or false);
          message = "services.slonnect runs the KDE Connect daemon itself; disable services.kdeconnect.";
        }
      ];

      home.packages = [ cfg.package cfg.kdeconnectPackage ];

      # Same unit as Home Manager's services.kdeconnect, so switching is seamless.
      systemd.user.services.kdeconnect = {
        Unit = {
          Description = "Adds communication between your desktop and your smartphone";
          After = [ "graphical-session.target" ];
          PartOf = [ "graphical-session.target" ];
        };
        Install.WantedBy = [ "graphical-session.target" ];
        Service = {
          ExecStart = "${cfg.kdeconnectPackage}/bin/kdeconnectd";
          Restart = "on-abort";
        };
      };

      xdg.mimeApps.defaultApplications = lib.mkIf cfg.smsHandler {
        "x-scheme-handler/sms" = [ "dev.slonnect.Slonnect.desktop" ];
        "x-scheme-handler/smsto" = [ "dev.slonnect.Slonnect.desktop" ];
      };

      xdg.dataFile = lib.mkIf cfg.hideKdeConnectApps (lib.listToAttrs (map hiddenEntry hidden));
    }

    (lib.mkIf cfg.indicator {
      systemd.user.services.kdeconnect-indicator = {
        Unit = {
          Description = "kdeconnect-indicator";
          After = [ "graphical-session.target" "polybar.service" "taffybar.service" "stalonetray.service" ];
          PartOf = [ "graphical-session.target" ];
          Requires = [ "tray.target" ];
        };
        Install.WantedBy = [ "graphical-session.target" ];
        Service = {
          ExecStart = "${cfg.kdeconnectPackage}/bin/kdeconnect-indicator";
          Restart = "on-abort";
        };
      };
    })
  ]);
}
