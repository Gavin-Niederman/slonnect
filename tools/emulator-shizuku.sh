#!/usr/bin/env bash
# Installs Shizuku in the running emulator, starts its server over adb (needed after every
# boot, like on a phone without root) and grants Slonnect access, so the privileged phone
# nodes (Bluetooth connect/disconnect, Bluetooth power, shell) work in the emulator.
# Run through `just emulator-shizuku`; `just emulate` calls it unless NO_SHIZUKU=1.
set -euo pipefail
version="v13.6.0"
url="https://github.com/RikkaApps/Shizuku/releases/download/v13.6.0/shizuku-v13.6.0.r1086.2650830c-release.apk"
sha256="6e273ab0e991c4e79bc8b1bbb9b9dd739ccac1a8712a541a214078886b7b790f"
shizuku="moe.shizuku.privileged.api"
pkg="${SLONNECT_PKG:-dev.slonnect.mobile.debug}"
cache="${XDG_CACHE_HOME:-$HOME/.cache}/slonnect/shizuku"
apk="$cache/shizuku-$version.apk"

# --- Download (pinned, checksummed) --------------------------------------------------------
if ! echo "$sha256  $apk" | sha256sum -c --status 2>/dev/null; then
    mkdir -p "$cache"
    echo "downloading Shizuku $version"
    curl -fsSL -o "$apk.part" "$url"
    echo "$sha256  $apk.part" | sha256sum -c --status || { echo "Shizuku APK checksum mismatch" >&2; rm -f "$apk.part"; exit 1; }
    mv "$apk.part" "$apk"
fi

# --- Install ---------------------------------------------------------------------------------
if ! adb shell pm path "$shizuku" >/dev/null 2>&1; then
    adb install -r "$apk" >/dev/null
    echo "installed Shizuku $version"
fi

# --- Start the server (as the shell user, like `adb shell` on a real phone) --------------------
running() { adb shell ps -A -o NAME 2>/dev/null | tr -d '\r' | grep -qx shizuku_server; }
if ! running; then
    libdir="$(adb shell dumpsys package "$shizuku" | tr -d '\r' | sed -n 's/^ *legacyNativeLibraryDir=//p' | head -1)"
    abi="$(adb shell getprop ro.product.cpu.abi | tr -d '\r')"
    starter="$libdir/$abi/libshizuku.so"
    adb shell "$starter" >/dev/null 2>&1 || true
    for _ in $(seq 1 20); do running && break; sleep 0.5; done
    running || { echo "Shizuku server didn't start ($starter)" >&2; exit 1; }
    echo "Shizuku server running"
fi

# --- Grant Slonnect access -----------------------------------------------------------------
receiver="$pkg/dev.slonnect.mobile.bridge.DemoReceiver"
state() {
    adb logcat -c
    adb shell am broadcast -n "$receiver" --es cmd shizuku-state >/dev/null
    sleep 1
    adb logcat -d -s Slonnect/Node | sed -n 's/.*shizuku: //p' | tail -1 | tr -d '\r'
}
# Slonnect needs to be running for Shizuku to hand it the binder.
adb shell am start -n "$pkg/dev.slonnect.mobile.ui.SlonnectActivity" >/dev/null
sleep 2
s="$(state)"
if [[ "$s" == NEEDS_PERMISSION ]]; then
    adb shell am broadcast -n "$receiver" --es cmd shizuku-permission >/dev/null
    # Press "Allow" in Shizuku's dialog.
    for _ in $(seq 1 10); do
        sleep 1
        adb shell uiautomator dump /sdcard/slonnect-ui.xml >/dev/null 2>&1 || continue
        xml="$(adb shell cat /sdcard/slonnect-ui.xml)"
        bounds="$(grep -oE 'text="Allow[^"]*"[^>]*bounds="\[[0-9]+,[0-9]+\]\[[0-9]+,[0-9]+\]"' <<<"$xml" | head -1 | grep -oE '\[[0-9]+,[0-9]+\]\[[0-9]+,[0-9]+\]' || true)"
        if [[ -n "$bounds" ]]; then
            read -r x1 y1 x2 y2 < <(tr -c '0-9' ' ' <<<"$bounds")
            adb shell input tap $(((x1 + x2) / 2)) $(((y1 + y2) / 2))
            break
        fi
    done
    sleep 1
    s="$(state)"
fi
case "$s" in
    READY) echo "Shizuku: Slonnect has access" ;;
    *) echo "!! Shizuku state for Slonnect: ${s:-unknown}. Open Shizuku in the emulator and allow Slonnect." >&2; exit 1 ;;
esac
