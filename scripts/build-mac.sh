#!/usr/bin/env bash
# Builds Transcript Studio on macOS into "dist/Transcript Studio.app" and checks it works.
#   1. the speech engine (NVIDIA NeMo-Speech.cpp): Metal GPU on Apple Silicon, CPU on Intel Macs
#   2. the app itself
#   3. the .app bundle: engine in Contents/Resources/engine/<flavour>, models in Contents/Resources/models
#   4. a self-test: a short spoken test file (macOS "say", two voices) through ts-cli
# Needs: Xcode Command Line Tools and Homebrew (see docs/mac-agent.md).
# Usage: scripts/build-mac.sh [--skip-engine]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
skip_engine=0
[ "${1:-}" = "--skip-engine" ] && skip_engine=1

if [ "$(uname -m)" = "arm64" ]; then flavour=metal; preset=metal-asr; else flavour=cpu; preset=cpu-asr; fi
echo "==> Mac type: $(uname -m) -> engine flavour: $flavour"

brew install cmake ninja sentencepiece abseil
git submodule update --init third_party/NeMo-Speech.cpp
git -C third_party/NeMo-Speech.cpp submodule update --init llama.cpp

# 1. engine, installed into a staging prefix so its libraries find each other wherever they are copied
eng_src=third_party/NeMo-Speech.cpp
eng_install="$root/build/engine-$flavour"
if [ $skip_engine -eq 0 ] || [ ! -d "$eng_install/lib" ]; then
  ( cd "$eng_src" && scripts/configure.sh "$preset" && cmake --build --preset "$preset" )
  rm -rf "$eng_install"
  cmake --install "$eng_src/build/$preset" --prefix "$eng_install"
fi

# 2. app
cmake -S . -B build/app -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/app

# 3. bundle
[ -f models/parakeet-tdt-0.6b-v3.q8_0.gguf ] && [ -f models/Nemotron-3-Diarization.q8_0.gguf ] || scripts/get-models.sh
app="dist/Transcript Studio.app"
rm -rf "$app" && mkdir -p dist && cp -R "build/app/TranscriptStudio.app" "$app"
res="$app/Contents/Resources"
mkdir -p "$res/engine/$flavour" "$res/models"
find "$eng_install/lib" "$eng_install/bin" -maxdepth 1 \( -name "*.dylib" -o -name "*.metallib" \) \
  -exec cp -P {} "$res/engine/$flavour/" \; 2>/dev/null || true
cp models/*.gguf "$res/models/"
cp build/app/ts-cli "$res/" 2>/dev/null || true
ls "$res/engine/$flavour" | grep -q nemo_speech_asr_c || { echo "ERROR: engine library missing in $res/engine/$flavour"; exit 1; }

# 4. self-test with a generated two-voice recording (no private audio involved)
test_dir="$root/build/selftest"
mkdir -p "$test_dir"
say -v Samantha -o "$test_dir/a.aiff" "Hello, this is a short test of the transcription app. How are you today?"
say -v Daniel -o "$test_dir/b.aiff" "I am fine, thank you. The weather in London has been quite nice this week."
cat > "$test_dir/join.txt" <<EOF
file 'a.aiff'
file 'b.aiff'
EOF
if command -v ffmpeg >/dev/null; then
  ffmpeg -loglevel error -y -f concat -safe 0 -i "$test_dir/join.txt" "$test_dir/test.wav"
else
  afconvert -f WAVE -d LEI16@16000 "$test_dir/a.aiff" "$test_dir/test.wav"  # one voice if ffmpeg is missing
fi
echo "==> self-test"
"$res/ts-cli" "$test_dir/test.wav" --engine "$flavour=$res/engine/$flavour" --models "$res/models" --out "$test_dir/result.json"
echo
echo "Built: $root/$app"
