# Slonnect

Material Design KDE Connect clients for the desktop and Android, plus a few protocol extensions
that let the two do more together.

| Directory | What |
|---|---|
| [`desktop/`](desktop/README.md) | The desktop app (Qt 6 / QML): messages (SMS, RCS through Google Messages for Web), notifications, files, clipboard, media, remote input and the node-graph action editor. Talks to kdeconnectd over D-Bus. |
| [`mobile/`](mobile/SLONNECT.md) | The Android app, a fork of kdeconnect-android 1.37.3: a new Compose home screen, phone-side action nodes (incl. Bluetooth through Shizuku), the RCS Messages bridge, a smoother presenter pointer. |
| [`protocol/`](protocol/README.md) | The `kdeconnect.slonnect.*` packets both apps speak, and the extra fields the bridge adds to SMS packets. |
| `nix/` | Packages, the patched kdeconnect-kde (`kdeconnect.nix` + `*.patch`), the Home Manager module, the Android dev shell. |
| `tools/` | Scripts behind the `just` recipes. |

What the pieces add up to:

- **Actions across both devices.** Build a graph on the computer from desktop, phone and logic
  nodes, then run it from either device. *Run in parallel* runs branches on both at the same
  time. The **Earbuds swap** template moves Bluetooth earbuds between the computer and the phone.
- **RCS two ways.** Google Messages for Web (`desktop/helpers/gmessages`: full history, needs a
  Google sign-in) or the phone's Messages bridge, which reads Google Messages' notifications and
  replies through it, with no account and nothing leaving the LAN.
- **A smoother laser pointer.** No Nagle batching on either end, gyroscope integration and
  filtering on the phone, and per-frame easing in the desktop overlay.

## Tasks

Everything runs in the flake's dev shells; you only need Nix (`nix develop -c just …` if `just`
isn't installed).

```sh
just build            # desktop app, patched kdeconnectd, Google Messages helper, Android debug APK
just test-desktop     # engine tests, Go helper tests, demo-mode render of every desktop page
just test-mobile      # Android unit tests
just run-desktop      # desktop app with synthetic data
just emulate          # Android emulator + app, connected to this computer, with fake RCS history
just fake-rcs "Grace Hopper" "Hi!"   # a fake incoming RCS message in the emulator
just dev-daemon       # run the patched kdeconnectd in the foreground instead of the user service
just upstream-diff --stat            # what mobile/ changes relative to kdeconnect-android
```

`just emulate` needs this computer's kdeconnectd to be the patched one (Home Manager module or
`just dev-daemon`): stock kdeconnectd ignores identity packets from 127.0.0.1, which is where the
emulator's packets come from. Then accept the pairing request in Slonnect.

## Testing the desktop and phone together

Three terminals, all from the repo root, all running your working tree:

```sh
# 1. Your patched daemon instead of the kdeconnect user service (restored when you Ctrl-C).
#    SLONNECT_BIN makes phone-triggered actions start your build when the app isn't open.
SLONNECT_BIN=$PWD/desktop/build/slonnect just dev-daemon

# 2. The desktop app from your build. Not in demo mode: demo mode never talks to a phone.
#    Quit any installed Slonnect first, or this launch just hands over to it.
just build-desktop && desktop/build/slonnect

# 3. The phone: the emulator (fake RCS, Shizuku, connects to this computer by itself) …
just emulate
#    … or a real phone on the same network: just build-mobile, then
#    adb install -r -t mobile/build/outputs/apk/debug/kdeconnect-android-debug.apk
```

Accept the pairing request (banner in Slonnect, or *Pair* on the phone). Then:

- **Actions:** the desktop's Actions page lists the phone's nodes under *On the phone*; *Earbuds
  swap* builds the example. The phone's *Actions* tab shows the desktop's actions; tap one to run it.
- **RCS bridge:** the emulator's fake RCS threads show up in Slonnect's Messages page (KDE Connect
  backend). `just fake-rcs "Grace Hopper" "Hi"` arrives live, and replying from the desktop goes
  back through the bridge.
- **One phone node without the desktop:** `just phone-node shell '{"command":"id"}'`.

After changes: desktop → rebuild and restart the app (terminal 2); a `nix/*.patch` → Ctrl-C and
rerun terminal 1; mobile → `just emulate` again (reuses a running emulator and reinstalls).
Shizuku has to be restarted after every emulator boot; `just emulate` and `just emulator-shizuku`
do that.

Logs: `adb logcat -s Slonnect Slonnect/Bridge Slonnect/Node Slonnect/Privileged` on the phone;
`QT_LOGGING_RULES='slonnect.link.debug=true;kdeconnect.plugin.slonnect.debug=true'` for the
desktop app and daemon.

## Install

Desktop (NixOS + Home Manager), replacing `services.kdeconnect`:

```nix
# flake inputs: slonnect.url = "...";
home-manager.users.you = {
  imports = [ inputs.slonnect.homeManagerModules.default ];
  services.slonnect.enable = true;
};
```

This runs the patched kdeconnectd. The patch adds the `slonnect` plugin the phone app's actions
and nodes need. Phone: `just build-mobile` and install
`mobile/build/outputs/apk/debug/kdeconnect-android-debug.apk`, or add your own signing config
for a release build. For Bluetooth and shell nodes, install and start
[Shizuku](https://shizuku.rikka.app); Slonnect's Setup tab walks through the rest.

## Licences

`desktop/helpers/gmessages`: AGPL-3.0 (libgm). `mobile/`: GPL-2.0-only
or GPL-3.0-only, like kdeconnect-android. `nix/*.patch`: same terms as kdeconnect-kde.
