# Slonnect monorepo tasks. `just` lists them.
# Everything runs inside the flake's dev shells, so the only prerequisite is Nix.

set shell := ["bash", "-euo", "pipefail", "-c"]

apk := "mobile/build/outputs/apk/debug/kdeconnect-android-debug.apk"

default:
    @just --list

# Build everything: desktop app, patched kdeconnectd, Google Messages helper, Android debug APK
build: build-desktop build-daemon build-gm build-mobile

# Desktop app (desktop/build/slonnect)
build-desktop:
    nix develop -c bash -c 'cd desktop && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && ninja -C build'

# Patched kdeconnect-kde (slonnect plugin, smooth presenter, LAN tweaks) → ./result-kdeconnect
build-daemon:
    nix build .#kdeconnect -o result-kdeconnect

# Google Messages for Web helper → ./result-gm
build-gm:
    nix build .#gmessages -o result-gm

# Android app, debug build (signed with the repo's debug key)
build-mobile:
    nix develop .#android -c bash -c 'cd mobile && ./gradlew --console=plain assembleDebug'

# Android app, release build (unsigned unless you add a signing config)
build-mobile-release:
    nix develop .#android -c bash -c 'cd mobile && ./gradlew --console=plain assembleRelease'

# Desktop tests: action engine unit tests, the Go helper's tests, and a demo-mode render of every page
test-desktop: build-desktop
    nix develop -c bash -c 'cd desktop && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure'
    nix develop -c bash -c 'cd desktop/helpers/gmessages && go test ./...'
    tools/smoke-desktop.sh

# Android unit tests (Robolectric)
test-mobile:
    nix develop .#android -c bash -c 'cd mobile && ./gradlew --console=plain testDebugUnitTest'

# All tests
test: test-desktop test-mobile

# Desktop app with synthetic messages, notifications and phone nodes (touches no real data)
run-desktop: build-desktop
    nix develop -c bash -c 'SLONNECT_DEMO=1 desktop/build/slonnect'

# Android emulator with the app installed, connected to this computer's kdeconnectd, with fake RCS history.
# HEADLESS=1 for no window. Needs the patched daemon running (see `just dev-daemon`).
emulate: build-mobile
    nix develop .#android-emulator -c tools/emulate.sh {{apk}}

# Install and start Shizuku in the emulator and let Slonnect use it (again after each emulator boot)
emulator-shizuku:
    nix develop .#android -c tools/emulator-shizuku.sh

# Run one phone node in the emulator without a desktop, e.g. just phone-node shell '{"command":"id"}'
phone-node node inputs="{}":
    nix develop .#android -c tools/phone-node.sh "{{node}}" '{{inputs}}'

# A fake incoming RCS message in the emulator, captured by the Messages bridge like a real one
fake-rcs from="Ada Lovelace" text="Are we still on for Thursday?" group="":
    nix develop .#android -c bash -c 'adb shell am broadcast -n dev.slonnect.mobile.debug/dev.slonnect.mobile.bridge.DemoReceiver --es cmd incoming --es from "$1" --es text "$2" ${3:+--es group "$3"}' _ "{{from}}" "{{text}}" "{{group}}"

# A fake incoming SMS in the emulator (lands in the real SMS provider, so it syncs as plain SMS)
fake-sms number="+15555550199" text="Your code is 123456":
    nix develop .#android -c adb emu sms send {{number}} "{{text}}"

# Reset the emulator's demo RCS history
fake-rcs-reset:
    nix develop .#android -c adb shell am broadcast -n dev.slonnect.mobile.debug/dev.slonnect.mobile.bridge.DemoReceiver --es cmd seed

# Screenshot of the emulator → emulator.png
emulator-shot:
    nix develop .#android -c bash -c 'adb exec-out screencap -p > emulator.png' && echo emulator.png

# Stop the emulator
emulator-stop:
    nix develop .#android -c adb emu kill

# Run the patched kdeconnectd in the foreground instead of the user service (restored on exit)
dev-daemon: build-daemon
    tools/dev-daemon.sh result-kdeconnect

# Diff of mobile/ against the kdeconnect-android release it was imported from
upstream-diff:
    tools/upstream-diff.sh
