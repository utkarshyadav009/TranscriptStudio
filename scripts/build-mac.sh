#!/usr/bin/env bash
# Builds Transcript Studio on macOS (Apple Silicon) into dist/Transcript Studio.app
#   1. the speech engine (NVIDIA NeMo-Speech.cpp) with the Metal GPU backend
#   2. the app itself
#   3. the .app bundle: engine in Contents/Resources/engine/metal, models in Contents/Resources/models
# Needs: Xcode Command Line Tools (xcode-select --install) and Homebrew.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

brew install cmake ninja sentencepiece abseil
git submodule update --init third_party/NeMo-Speech.cpp
git -C third_party/NeMo-Speech.cpp submodule update --init llama.cpp

# 1. engine (Metal); NeMo-Speech.cpp's own preset
( cd third_party/NeMo-Speech.cpp && scripts/configure.sh metal-asr && cmake --build --preset metal-asr )

# 2. app
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 3. bundle
[ -f models/parakeet-tdt-0.6b-v3.q8_0.gguf ] || scripts/get-models.sh
app="dist/Transcript Studio.app"
rm -rf "$app" && mkdir -p dist && cp -R "build/TranscriptStudio.app" "$app"
res="$app/Contents/Resources"
mkdir -p "$res/engine/metal" "$res/models"
eng="third_party/NeMo-Speech.cpp/build/metal-asr"
find "$eng/bin" "$eng/lib" -maxdepth 1 \( -name "*.dylib" -o -name "*.metal" -o -name "*.metallib" \) -exec cp -P {} "$res/engine/metal/" \; 2>/dev/null || true
cp models/*.gguf "$res/models/"
echo "Built: $app"
echo "First launch: right-click the app > Open (it is not signed with an Apple developer ID)."
