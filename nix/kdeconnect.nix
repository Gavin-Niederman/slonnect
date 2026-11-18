# kdeconnect-kde with the fixes Slonnect relies on. Only the daemon side matters to
# Slonnect (it replaces the official app and SMS client), but the package is otherwise
# stock, so it can stand in for `kdePackages.kdeconnect-kde` anywhere.
{ kdePackages }:

kdePackages.kdeconnect-kde.overrideAttrs (old: {
  patches = (old.patches or [ ]) ++ [
    # Presenter laser pointer: transparent wlr-layer-shell overlay on Wayland instead
    # of a fullscreen toplevel (black backdrop on niri and others).
    ./kdeconnect-presenter-layer-shell.patch
  ];
  buildInputs = (old.buildInputs or [ ]) ++ [ kdePackages.layer-shell-qt ];
})
