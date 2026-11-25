#!/usr/bin/env bash
# Downloads the Material Icons listed in qml/icons/icons.txt from Iconify's
# "ic" collection (Google Material Icons, Apache-2.0) as outlined + filled SVGs.
# Uses one batched JSON request per style to stay well under API rate limits.
# Needs curl and jq (e.g. `nix shell nixpkgs#jq`).
set -euo pipefail
dir="$(cd "$(dirname "$0")/../qml/icons" && pwd)"
names=$(grep -v '^\s*#' "$dir/icons.txt" | grep -v '^\s*$')

for style in outlined:outline filled:baseline; do
    folder="$dir/${style%%:*}" prefix="${style##*:}"
    rm -rf "$folder" && mkdir -p "$folder"
    ids=$(echo "$names" | sed "s/_/-/g; s/^/$prefix-/" | paste -sd, -)
    json=$(curl -fsS --retry 5 --retry-delay 5 "https://api.iconify.design/ic.json?icons=$ids")
    missing=$(jq -r '.not_found // [] | join(" ")' <<<"$json")
    [[ -z "$missing" ]] || { echo "not in Iconify ic set: $missing" >&2; exit 1; }
    while read -r name; do
        id="$prefix-${name//_/-}"
        jq -r --arg id "$id" '
            (.icons[$id] // error("no " + $id)) as $i
            | "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 \($i.width // .width // 24) \($i.height // .height // 24)\">\($i.body)</svg>"
        ' <<<"$json" >"$folder/$name.svg"
    done <<<"$names"
done
echo "icons: $(ls "$dir/outlined" | wc -l) outlined, $(ls "$dir/filled" | wc -l) filled"
