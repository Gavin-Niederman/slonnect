# kdeconnect-kde with the fixes Slonnect relies on. Only the daemon side matters to
# Slonnect (it replaces the official app and SMS client), but the package is otherwise
# stock, so it can stand in for `kdePackages.kdeconnect-kde` anywhere.
{ kdePackages }:

kdePackages.kdeconnect-kde.overrideAttrs (old: {
  patches = (old.patches or [ ]) ++ [
    # Presenter laser pointer: transparent wlr-layer-shell overlay on Wayland instead
    # of a fullscreen toplevel (black backdrop on niri and others).
    ./kdeconnect-presenter-layer-shell.patch
    # KDECONNECT_APP / KDECONNECT_SMS_APP: let another client stand in for kdeconnect-app
    # (pairing notification, the phone's "add command") and kdeconnect-sms.
    ./kdeconnect-app-override.patch
    # plugins/slonnect: forwards kdeconnect.slonnect.* packets (actions, phone-side nodes)
    # between the Slonnect desktop app on D-Bus and the Slonnect Android app.
    # See protocol/README.md.
    ./kdeconnect-slonnect-plugin.patch
    # Presenter pointer eases toward each new position every frame instead of jumping.
    ./kdeconnect-presenter-smooth.patch
    # TCP_NODELAY on LAN links; accept identity packets from loopback (Android emulator).
    ./kdeconnect-lan-tweaks.patch
  ];
  buildInputs = (old.buildInputs or [ ]) ++ [ kdePackages.layer-shell-qt ];
})
