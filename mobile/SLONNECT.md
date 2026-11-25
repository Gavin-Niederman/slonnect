# Slonnect for Android

A fork of the KDE Connect Android app. It still speaks plain KDE Connect, so it works with stock
KDE Connect desktops too; with Slonnect on the desktop it adds actions that run on both devices
and RCS messages without Google Messages for Web.

Upstream tag: v1.37.3
Upstream commit: f82d035ddc93d41aa45bea3988a6db4265c5554f
Upstream repository: https://invent.kde.org/network/kdeconnect-android

New code lives in `src/main/java/dev/slonnect/mobile/`. Changes to upstream files are small and
marked with a `Slonnect:` comment; `just upstream-diff --stat` lists them. To pull a newer
release, diff the two upstream tags and apply that diff here (`git apply -3 --directory=mobile`).

| Application id | `dev.slonnect.mobile` (`.debug` for debug builds) |
|---|---|
| Min Android | 7.0 (API 24, Shizuku needs it; upstream is 23) |
| Build | `just build-mobile` → `mobile/build/outputs/apk/debug/` |

The id differs from KDE Connect's (`org.kde.kdeconnect_tp`), so it installs next to it. Running
both makes the computer see two phones; uninstall KDE Connect, or turn it off, once Slonnect is paired.

## What's different

**Home screen** (`ui/`): a Compose, Material 3 home with dynamic colour. Each computer gets a card
with connection and battery state, its Slonnect actions as one-tap tiles, and its plugin
controls as chips. There's an *Actions* tab with every computer's actions as big tiles, which
show progress and the last result, and a *Setup* tab listing each permission a feature needs,
with one tap to fix it. Pairing, plugin settings, the touchpad, media remote and so on are still
KDE Connect's screens, opened from here.

**Actions on the phone** (`SlonnectPlugin`, `nodes/PhoneNodes.kt`): the phone tells the desktop
which nodes it can run (protocol/README.md). They show up in the desktop's action editor under
*On the phone*, and a graph can mix them with desktop nodes, one after another or at the same
time with *Run in parallel*.

| Node | Needs |
|---|---|
| Phone Bluetooth device (connect / disconnect / toggle), Bluetooth connected? | Nearby devices permission; Shizuku on Android 12+ to connect or disconnect |
| Phone Bluetooth power | Shizuku on Android 13+ |
| Phone volume, Ringer mode, Do Not Disturb | Do Not Disturb access for silent / DND (or Shizuku) |
| Flashlight, Phone media key, Vibrate, Phone notification, Phone clipboard | – |
| Open on phone (app or link) | "Display over other apps" for opening from the background |
| Send intent (Tasker, MacroDroid, …) | – |
| Phone shell | Shizuku |

Android only lets system apps connect another device's audio profiles, so swapping earbuds
needs [Shizuku](https://shizuku.rikka.app). Slonnect's helper (`privileged/PrivilegedService`)
runs in a process Shizuku starts as the adb shell user, which holds `BLUETOOTH_PRIVILEGED`.
Without Shizuku the node tries the hidden connect calls directly, which works on Android 11 and
older and on some ROMs, and otherwise says Shizuku is needed.

**Messages bridge** (`bridge/`): another way to get RCS in and out of the phone. Android has no
RCS API for third-party apps, and Google Messages keeps RCS chats in its private database, so
they never reach the SMS provider KDE Connect reads. The bridge:

- reads Google Messages' (and Samsung Messages') conversation notifications: sender, text,
  time, group name, and the earlier messages the notification shows. It drops whatever is
  already in the SMS provider, so only RCS and other non-SMS messages are added, and keeps them
  in a private database (`BridgeStore`);
- merges them into the regular SMS plugin packets: into the person's SMS thread when there is
  one, otherwise into a bridge-only thread. kdeconnectd and every SMS client on the computer see
  them as ordinary conversations, with no desktop changes;
- sends from the computer through Google Messages, so RCS is used whenever Google Messages would
  use it. It fires the notification's own reply action when there is one (works with the screen
  off). Otherwise it opens the chat with the text filled in, and the *Send messages*
  accessibility service presses Send (needs the phone unlocked). Setup → *Send through Google
  Messages* picks when: only RCS chats (default), always, or never.

What it can't do: messages you read on the phone before a notification was posted, or send from
the phone itself, are never seen. Images arrive as "[image]". Group members without a saved
contact show by name. For complete history, Google Messages for Web (desktop/helpers/gmessages)
is still the way.

**Presenter pointer:** gyroscope readings are integrated over real time, de-jittered (One-Euro
filter) and sent at up to ~120 Hz (`PointerSmoother`). The desktop eases between packets
(`nix/kdeconnect-presenter-smooth.patch`), and both ends now disable Nagle's algorithm, which had
been delivering the pointer packets in bursts.

**Emulator:** in the Android emulator the app also introduces itself to the host (10.0.2.2) and
listens on TCP 1760, which `just emulate` forwards, so it connects to the computer's kdeconnectd
(`Emulator.kt`).

## Demo data

Debug builds can fill the bridge with synthetic RCS history (invented people, 555-01xx numbers)
and post fake Google-Messages-style notifications that go through the real capture and reply
code, plus a stand-in compose screen for the accessibility path. It's in Setup → *Demo*, or over adb:

```sh
just fake-rcs-reset                                   # seed history (turns the bridge on in demo mode)
just fake-rcs "Grace Hopper" "Hello!"                  # incoming 1:1
just fake-rcs "Alan Turing" "Rope?" "Climbing crew"    # incoming group message
just fake-sms +15555550199 "Hi"                        # a real SMS into the emulator's provider
```

Demo mode uses its own database, so it never mixes with real bridged messages.
