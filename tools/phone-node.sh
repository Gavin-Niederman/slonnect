#!/usr/bin/env bash
# Runs one phone node in the emulator (debug build) without a desktop and prints its result.
#   tools/phone-node.sh shell '{"command":"id"}'
set -euo pipefail
node="${1:?node id, e.g. shell}"
inputs="${2:-}"
[[ -n "$inputs" ]] || inputs='{}'
pkg="${SLONNECT_PKG:-dev.slonnect.mobile.debug}"
adb logcat -c
# adb shell runs its arguments through the device's sh, so single-quote the JSON for it
# (each ' inside becomes '\'').
sq="'\\''"
quoted="'${inputs//\'/$sq}'"
adb shell am broadcast -n "$pkg/dev.slonnect.mobile.bridge.DemoReceiver" --es cmd node --es node "$node" --es inputs "$quoted" >/dev/null
for _ in $(seq 1 60); do
    out="$(adb logcat -d -s Slonnect/Node | grep -E "$node (ok|failed)" | tail -1 || true)"
    if [[ -n "$out" ]]; then
        echo "${out#*Slonnect/Node: }"
        [[ "$out" == *" ok "* ]]
        exit
    fi
    sleep 0.5
done
echo "no result from the phone within 30 s" >&2
exit 1
