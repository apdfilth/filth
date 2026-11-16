#!/bin/bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <local ZIP or user@host:/path/to/Filth-macos-vst3.zip>" >&2
    exit 2
fi

plugin_dir="$HOME/Library/Audio/Plug-Ins/VST3"
destination="$plugin_dir/Filth.vst3"
mkdir -p "$plugin_dir"
staging=$(mktemp -d "$plugin_dir/.filth-install.XXXXXX")

cleanup() {
    if [[ -e "$staging/previous" || -L "$staging/previous" ]]; then
        if [[ ! -e "$destination" && ! -L "$destination" ]]; then
            mv -- "$staging/previous" "$destination" || return 1
        fi
    fi
    rm -rf -- "$staging"
}
trap cleanup EXIT

archive=$1
if [[ ! -f "$archive" && "$archive" == *:* ]]; then
    scp -- "$archive" "$staging/download.zip"
    archive="$staging/download.zip"
fi

ditto -x -k "$archive" "$staging/unpacked"
bundle="$staging/unpacked/Filth.vst3"
codesign --verify --deep --strict "$bundle"

if [[ -e "$destination" || -L "$destination" ]]; then
    mv -- "$destination" "$staging/previous"
fi
mv -- "$bundle" "$destination"
printf 'Installed %s\n' "$destination"
