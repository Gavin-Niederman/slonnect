#!/usr/bin/env bash
# Renders every page of the desktop app in demo mode (synthetic data only) without a
# display and fails on QML errors. Screenshots land in desktop/build/smoke/.
set -euo pipefail
cd "$(dirname "$0")/.."
[[ -n "${QML_IMPORT_PATH:-}" ]] || exec nix develop -c "$0" "$@"
cd desktop
mkdir -p build/smoke
fail=0
for page in messages notifications files clipboard media remote actions device settings; do
    log="build/smoke/$page.log"
    QT_QPA_PLATFORM=offscreen SLONNECT_DEMO=1 SLONNECT_PAGE=$page SLONNECT_SCREENSHOT=build/smoke/$page.png \
        SLONNECT_SCREENSHOT_DELAY=2500 timeout 40 build/slonnect >"$log" 2>&1 || true
    if grep -qE '\.qml:[0-9]+.*(Error|TypeError|ReferenceError|is not defined|Cannot)' "$log" || [[ ! -s "build/smoke/$page.png" ]]; then
        echo "FAIL $page (see desktop/$log)"; grep -E '\.qml:[0-9]+' "$log" | head -5 || true; fail=1
    else
        echo "ok   $page"
    fi
done
exit $fail
