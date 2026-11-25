#!/usr/bin/env bash
# Boots an Android emulator, installs the Slonnect debug APK, grants what the demo needs,
# seeds fake RCS history and points the app at this computer's kdeconnectd.
# Run through `just emulate` (needs the android-emulator dev shell).
#
# How the connection works: the emulator is behind NAT, so the app (in the emulator only)
# introduces itself to 10.0.2.2 — the host, seen by kdeconnectd as 127.0.0.1 — and listens on
# TCP 1760, which `adb forward` exposes on the host. kdeconnectd then connects to 127.0.0.1:1760.
# Accepting identities from loopback needs the patched daemon (nix/kdeconnect-lan-tweaks.patch).
set -euo pipefail
apk="${1:?apk path}"
avd="${SLONNECT_AVD:-slonnect}"
api="${SLONNECT_EMULATOR_API:-36}"
image="system-images;android-${api};google_apis;x86_64"
pkg="dev.slonnect.mobile.debug"
port=1760

[[ -f "$apk" ]] || { echo "no APK at $apk; run just build-mobile" >&2; exit 1; }
emulator="$ANDROID_HOME/emulator/emulator"
avdmanager="$(ls -d "$ANDROID_HOME"/cmdline-tools/*/bin | head -1)/avdmanager"

# --- AVD ---------------------------------------------------------------------------------
if ! "$emulator" -list-avds | grep -qx "$avd"; then
    echo "creating AVD $avd ($image)"
    echo no | "$avdmanager" create avd --force -n "$avd" -k "$image" -d pixel_7 >/dev/null
    cfg="${ANDROID_AVD_HOME:-$HOME/.android/avd}/$avd.avd/config.ini"
    # Real keyboard, more RAM, a writable SD card for received files.
    sed -i 's/^hw.keyboard=.*/hw.keyboard=yes/' "$cfg" 2>/dev/null || true
    grep -q '^hw.keyboard=' "$cfg" || echo 'hw.keyboard=yes' >>"$cfg"
    echo 'hw.ramSize=4096' >>"$cfg"
fi

# --- Boot ----------------------------------------------------------------------------------
if ! adb devices | grep -q '^emulator-'; then
    flags=(-avd "$avd" -no-snapshot-save -no-boot-anim -gpu swiftshader_indirect -netdelay none -netspeed full)
    [[ -n "${HEADLESS:-}" ]] && flags+=(-no-window -no-audio)
    log="${XDG_RUNTIME_DIR:-/tmp}/slonnect-emulator.log"
    echo "starting emulator (log: $log)"
    # Own session, so Ctrl-C in (or the end of) the calling terminal doesn't take it down.
    setsid -f "$emulator" "${flags[@]}" >"$log" 2>&1 </dev/null
fi
adb wait-for-device
echo -n "waiting for Android to boot"
until [[ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" == 1 ]]; do echo -n .; sleep 2; done
echo

# --- App -----------------------------------------------------------------------------------
adb install -r -t -g "$apk" >/dev/null
echo "installed $pkg"
# What the demo needs: SMS/contacts for the SMS plugin and bridge, notification access to
# capture (fake) Google Messages notifications, the accessibility service for sending, DND.
for p in READ_SMS SEND_SMS RECEIVE_SMS READ_CONTACTS READ_PHONE_STATE POST_NOTIFICATIONS BLUETOOTH_CONNECT; do
    adb shell pm grant "$pkg" "android.permission.$p" 2>/dev/null || true
done
adb shell cmd notification allow_listener "$pkg/org.kde.kdeconnect.plugins.notifications.NotificationReceiver" || true
adb shell cmd notification allow_dnd "$pkg" || true
adb shell settings put secure enabled_accessibility_services "$pkg/dev.slonnect.mobile.bridge.BridgeAccessibilityService"
adb shell settings put secure accessibility_enabled 1

# --- Connection to this computer -----------------------------------------------------------
adb forward "tcp:$port" "tcp:$port" >/dev/null
adb shell am start -n "$pkg/dev.slonnect.mobile.ui.SlonnectActivity" >/dev/null
sleep 2
adb shell am broadcast -n "$pkg/dev.slonnect.mobile.bridge.DemoReceiver" --es cmd seed >/dev/null
echo "seeded fake RCS history"

# Shizuku, for the privileged phone nodes (Bluetooth connect/disconnect, shell).
if [[ -z "${NO_SHIZUKU:-}" ]]; then
    "$(dirname "$0")/emulator-shizuku.sh" || echo "!! Shizuku setup failed; retry with: just emulator-shizuku" >&2
fi

# Match on the command line: on NixOS the process can be named .kdeconnectd-wrapped.
daemon_pid="$(pgrep -u "$(id -u)" -f 'bin/\.?kdeconnectd(-wrapped)?( |$)' | head -1 || true)"
if [[ -z "$daemon_pid" ]]; then
    echo "!! kdeconnectd isn't running on this computer; start it (or: just dev-daemon)" >&2
elif ! (
        # The slonnect plugin next to the daemon, or in a directory on its QT_PLUGIN_PATH.
        dirs="$(dirname "$(readlink -f "/proc/$daemon_pid/exe")")/../lib/qt-6/plugins"
        dirs+=":$(tr '\0' '\n' <"/proc/$daemon_pid/environ" 2>/dev/null | sed -n 's/^QT_PLUGIN_PATH=//p')"
        IFS=: read -ra list <<<"$dirs"
        for d in "${list[@]}"; do [[ -n "$d" && -e "$d/kdeconnect/kdeconnect_slonnect.so" ]] && exit 0; done
        exit 1
    ) 2>/dev/null; then
    echo "!! the running kdeconnectd isn't Slonnect's patched one, so it ignores the emulator." >&2
    echo "   Use the Home Manager module from this flake, or run: just dev-daemon" >&2
fi

cat <<MSG

Emulator ready. The app announces itself to this computer every few seconds:
  - accept the pairing request in Slonnect (desktop) or tap Pair in the emulator
  - fake RCS:   just fake-rcs "Grace Hopper" "Hello!"      (group: just fake-rcs "Alan Turing" "Rope?" "Climbing crew")
  - fake SMS:   just fake-sms +15555550199 "Hi"
  - screenshot: just emulator-shot      stop: just emulator-stop
MSG
