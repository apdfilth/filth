#!/bin/bash
set -euo pipefail

root=$(cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="$root/build/macos-vst3"
out_dir="$root/out"
bundle="$build_dir/Filth_artefacts/Release/VST3/Filth.vst3"
archive="$out_dir/Filth-macos-vst3.zip"

cmake -S "$root" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DFILTH_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build "$build_dir" --target Filth_VST3 --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-6}"

lipo "$bundle/Contents/MacOS/Filth" -verify_arch arm64 x86_64
test -s "$bundle/Contents/Resources/moduleinfo.json"

mkdir -p "$out_dir"
staging=$(mktemp -d "$out_dir/.filth-package.XXXXXX")
trap 'rm -rf -- "$staging"' EXIT
packaged_bundle="$staging/Filth.vst3"
ditto "$bundle" "$packaged_bundle"
# JUCE adds the VST3 manifest after its initial signing step.
codesign --force --sign - "$packaged_bundle"
codesign --verify --deep --strict "$packaged_bundle"
ditto -c -k --keepParent "$packaged_bundle" "$staging/Filth-macos-vst3.zip"
mv -f -- "$staging/Filth-macos-vst3.zip" "$archive"
shasum -a 256 "$archive"
