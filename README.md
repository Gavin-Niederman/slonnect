# Slonnect

A Material Design 3 desktop client that merges the KDE Connect app and KDE Connect SMS into one window.
It talks to the running `kdeconnectd` over D-Bus. It doesn't pair or speak the network protocol
itself, so it works alongside the stock KDE Connect tools.

| Page      | KDE Connect plugin(s)                | What it does |
|-----------|--------------------------------------|--------------|
| Messages  | `sms` (conversations), `contacts`    | SMS/MMS/RCS threads with paging, group chats, image/video/file attachments, sending with attachments, sharing contacts as vCards (and rendering received ones as cards), drafts per thread |
| Files     | `share`, `sftp`                      | Drag-and-drop or portal file picker to send, send links/text, received-files list, browse phone storage |
| Clipboard | `clipboard`                          | Push the clipboard to the phone, history with per-item resend, auto-sync status |
| Media     | `mprisremote`                        | Now playing with cover-art tint, seek, previous/play/next, volume, player switching |
| Actions   | `runcommand`                         | Node-graph actions you trigger from the phone's Run command list: ring/ping/SMS/share, desktop notifications, shell, MPRIS, D-Bus calls, branching, waits, text formatting |
| Device    | `battery`, `ping`, `findmyphone`, …  | Status, ring/ping, pairing, contact sync, and a list of plugins that aren't wired up yet |

## Build and run

```sh
nix build            # ./result/bin/slonnect
# or, for development:
nix develop -c bash -c 'cmake -B build -G Ninja && ninja -C build'
./build/slonnect
```

It needs Qt ≥ 6.7 (QtQuick, QuickControls2 Material, QtSvg, DBus). Icons are Google's Material Icons
(Apache-2.0) baked in as SVGs; add one by listing it in `qml/icons/icons.txt` and running `tools/fetch-icons.sh`.
Screen mirroring uses `scrcpy` and `adb`; the Nix package bundles both.
`sms:` / `smsto:` URLs passed as arguments open a draft to that number.

## Performance notes

- Every D-Bus call is asynchronous, so the UI thread never blocks on the daemon or the phone.
- C++ list models feed `ListView`s with `reuseItems`. Messages are stored newest-first and shown
  bottom-to-top, so loading older history appends to the end of the model instead of reshuffling it.
- vCard parsing, cover-art colour extraction and image decoding run off the GUI thread
  (`QtConcurrent`, async image provider).
- Media position is extrapolated in QML from one timestamp, so playback isn't polled.
- Pages are created on first visit and then kept alive.

## Adding a feature

The remaining KDE Connect plugins (notifications, remote input, presenter, …) are
small additions:

1. Subclass `DevicePlugin` (`src/deviceplugin.h`): pass the plugin id, D-Bus path suffix and
   interface, then `subscribe()` to signals in `attached()` and call methods with `invoke()`.
2. Expose it as a property on `App` (`src/app.h`) and add it to the list in `App::rebind()`.
3. Add a page under `qml/pages/` and an entry to `Features.destinations` (`qml/Features.qml`).
   Remove it from `Features.planned`.

## Actions

The Actions page builds small node graphs. Each enabled action is added to KDE Connect's
**Run command** list for every paired phone (KDE Connect on the phone → this computer → Run command).
Tapping it there runs the graph. Commands you added in KDE Connect's own settings are kept.

- **Execution** starts at the *Phone command* node and follows the white flow wires. Coloured data
  wires carry values; an unconnected input uses the value typed into the node. Pure nodes (Text,
  Format, Math, Date & time) are evaluated when something reads them.
- **Editor:** drag from a pin to connect, drag a connected input to move its wire, right-click a
  pin to disconnect, double-click or right-click empty space to add a node, Delete removes the
  selected node, Ctrl+D duplicates it. Wheel zooms, dragging the background pans.
- **How it hooks in:** kdeconnectd keeps the list in
  `~/.config/kdeconnect/<device>/kdeconnect_runcommand/config` and runs entries with `/bin/sh -c`.
  Slonnect owns the entries whose key starts with `slonnect-`; each one runs
  `slonnect --run-action <id> --device <device>`. That hands the action to the open Slonnect over
  D-Bus (`dev.slonnect.Slonnect`, `/Actions`, `dev.slonnect.Actions.Run`), so the editor shows it
  live. If Slonnect isn't open, it runs the action without a window and exits.
- Actions are stored in `~/.config/slonnect/slonnect/actions.json`. The *Run log* under the
  editor shows each step of the last run; failing nodes are outlined in red.
- To add a node type, append a `NodeType` in `src/nodetypes.cpp`. The editor picks it up from the
  catalogue.

## Nix and Home Manager

The flake ships Slonnect and a patched `kdeconnect-kde` (`packages.<system>.kdeconnect`, patches in
`nix/`). The patch makes the presenter laser pointer a transparent wlr-layer-shell overlay on
Wayland, instead of a fullscreen window that niri and others paint over a black backdrop.
The Home Manager module replaces `services.kdeconnect`:

```nix
# flake inputs: slonnect.url = "...";
home-manager.users.you = {
  imports = [ inputs.slonnect.homeManagerModules.default ];
  services.slonnect.enable = true;   # and drop services.kdeconnect
  # services.slonnect.indicator = true;           # KDE Connect tray icon
  # services.slonnect.hideKdeConnectApps = false; # keep KDE Connect's own app/SMS in launchers
};
```

It runs the patched `kdeconnectd` as the same `kdeconnect.service` user unit, installs Slonnect,
hides KDE Connect's own app and SMS client from launchers, and makes Slonnect the `sms:` handler
(when `xdg.mimeApps.enable` is on). Phones still need TCP/UDP 1714–1764 open. On NixOS,
`programs.kdeconnect.enable` (or a firewall rule) does that. `overlays.default` provides
`pkgs.slonnect` and `pkgs.slonnect-kdeconnect` for other setups.

## Troubleshooting

- **An action isn't on the phone:** the *Run command* plugin has to be enabled for this computer in
  KDE Connect on the phone and on this computer (the Actions page says which). Slonnect asks
  kdeconnectd to push the new list right away; reopening the device on the phone refreshes it.
- **0 contacts / "phone didn't answer":** on the phone, open KDE Connect → this computer →
  Plugin settings and enable **Contacts synchronizer**. The Android Contacts permission alone
  isn't enough, and the desktop can't tell the plugin is off. It just never gets a reply.
- **Browse phone** mounts the phone over SFTP and opens its storage at the local mount point
  (`/run/user/$UID/<device>/…`), so any file manager works, not just KIO-aware ones.
- File choosing goes through the XDG desktop portal (`org.freedesktop.portal.FileChooser`). Qt's
  own dialog is only used when no portal service is running.

## Screen mirroring

The Mirror screen button (Device page) runs scrcpy over adb. Pair the computer once with
`adb pair IP:PORT CODE` from the phone's Wireless debugging → "Pair device with pairing code".
After that, nothing needs to be done per reboot as long as Wireless debugging is on (Shizuku's
start-on-boot or an automation app can keep it on). To find the phone, Slonnect tries, in order:

1. an existing adb connection (USB or network);
2. the endpoint that worked last time;
3. port 5555 on the addresses KDE Connect knows (if you ran `adb tcpip 5555`);
4. mDNS via adb or Avahi (same local network only);
5. a quick scan of ports 30000–49999 on KDE Connect's addresses, then `adb connect` on what answers.
   This is what makes it work over Tailscale, where mDNS can't reach (about 8 s; the result is cached).

## Demo mode

`SLONNECT_DEMO=1` swaps in synthetic conversations and clipboard history and never reads real
messages, contacts or clipboard. Settings go to a separate `slonnect-demo` profile. For headless checks:

```sh
QT_QPA_PLATFORM=offscreen SLONNECT_DEMO=1 SLONNECT_PAGE=media SLONNECT_SCREENSHOT=out.png ./build/slonnect
```

(`SLONNECT_THREAD=<id>` opens a thread first.) The offscreen platform uses Qt's software renderer,
where image rounding is skipped. On a real GPU, images are clipped to rounded corners.
