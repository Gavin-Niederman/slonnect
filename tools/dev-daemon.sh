#!/usr/bin/env bash
# Runs the patched kdeconnectd from a build result in the foreground instead of the one that's
# running now, and puts the old one back on exit.
#
# The running daemon may come from the kdeconnect user service (Home Manager), an XDG autostart
# entry (app-org.kde.kdeconnect.daemon@autostart.service) or D-Bus activation. Activation
# restarts the stock daemon as soon as anything (Slonnect, the tray icon) calls
# org.kde.kdeconnect, so this keeps replacing it until our daemon owns the bus name.
set -euo pipefail
out="$(readlink -f "${1:?build result, e.g. result-kdeconnect}")"
daemon="$(find "$out" -name kdeconnectd -type f -perm -u+x | head -1)"
[[ -n "$daemon" ]] || { echo "no kdeconnectd in $out" >&2; exit 1; }

# On NixOS the process is .kdeconnectd-wrapped, so match the command line.
daemons() { pgrep -u "$(id -u)" -f 'bin/\.?kdeconnectd(-wrapped)?( |$)' || true; }
owner() {
    busctl --user call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus \
        GetConnectionUnixProcessID s org.kde.kdeconnect 2>/dev/null | awk '{print $2}'
}
descendant_of() { # pid ancestor
    local p="$1"
    while [[ -n "$p" && "$p" != 0 && "$p" != 1 ]]; do
        [[ "$p" == "$2" ]] && return 0
        p="$(awk '{print $4}' "/proc/$p/stat" 2>/dev/null || true)"
    done
    return 1
}

# Stop the unit the current daemon runs in, if it's a real (non-transient) one, so it doesn't
# respawn it; remember it to start again afterwards.
unit=""
for pid in $(daemons); do
    u="$(sed -n 's|^0::.*/\([^/]*\.service\)$|\1|p' "/proc/$pid/cgroup" 2>/dev/null || true)"
    case "$u" in
        kdeconnect.service | app-org.kde.kdeconnect*@autostart.service)
            echo "stopping $u"
            systemctl --user stop "$u"
            unit="$u" ;;
    esac
done

ours=""
cleanup() {
    [[ -n "$ours" ]] && kill "$ours" 2>/dev/null && wait "$ours" 2>/dev/null
    if [[ -n "$unit" ]]; then
        echo "starting $unit again"
        systemctl --user start "$unit" || true
    fi
    # Otherwise the next D-Bus call to org.kde.kdeconnect starts the normal daemon.
}
trap cleanup EXIT
trap 'exit 130' INT TERM

export QT_PLUGIN_PATH="$out/lib/qt-6/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
for attempt in 1 2 3 4 5; do
    for pid in $(daemons); do kill "$pid" 2>/dev/null || true; done
    for _ in $(seq 1 20); do [[ -z "$(daemons)" ]] && break; sleep 0.1; done
    "$daemon" &
    ours=$!
    for _ in $(seq 1 30); do [[ -n "$(owner)" ]] && break; sleep 0.2; done
    if descendant_of "$(owner)" "$ours"; then
        echo "running $daemon (pid $ours; Ctrl-C to stop)"
        wait "$ours"
        exit
    fi
    echo "a stock kdeconnectd took the bus name first; retrying ($attempt)"
    kill "$ours" 2>/dev/null || true
    wait "$ours" 2>/dev/null || true
    ours=""
done
echo "couldn't replace the running kdeconnectd" >&2
exit 1
