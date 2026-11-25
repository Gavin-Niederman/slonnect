#!/usr/bin/env bash
# Shows what Slonnect changed in mobile/ relative to the kdeconnect-android release it was
# imported from (see mobile/SLONNECT.md). Extra arguments go to git diff, e.g. --stat.
set -euo pipefail
cd "$(dirname "$0")/.."
tag="$(sed -n 's/^Upstream tag: *//p' mobile/SLONNECT.md)"
url="https://invent.kde.org/network/kdeconnect-android.git"
git fetch --depth 1 --no-tags "$url" "refs/tags/$tag:refs/slonnect/upstream" >/dev/null 2>&1
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/upstream" "$tmp/ours"
git archive refs/slonnect/upstream | tar -x -C "$tmp/upstream"
# Tracked and new (not ignored) files only, so build output doesn't show up.
git ls-files -z -co --exclude-standard mobile | xargs -0 cp --parents -t "$tmp/ours"
git diff --no-index "$@" "$tmp/upstream" "$tmp/ours/mobile" || true
