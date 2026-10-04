# Transcript Studio — notes for Claude Code

Local desktop app: give it any recording, it writes down what was said, tells the speakers
apart, and lets a non-technical person check and correct everything against the audio,
including a round trip through Microsoft Word with tracked changes. Interface in English and
Italian. Runs on CPU and GPU. Windows and macOS (Apple Silicon).

## Layout
- `src/` C++17 core + app
  - `engine.*` pipeline: audio split at pauses → Parakeet TDT v3 (ASR, word times) → Nemotron 3
    Diarization (one stream over the whole file) → each word gets the speaker that overlaps it most
  - `nemo_loader.*` loads the engine at run time from `engine/<flavour>/` (cuda, vulkan, metal, cpu)
    through NeMo-Speech's stable C ABI (`nemo_speech_asr_c` library). Nothing links the engine.
  - `audio_decode*` miniaudio for wav/mp3/flac; Media Foundation (Win) / AudioToolbox (Mac) for the rest
  - `player.*` miniaudio playback (follows the default output device)
  - `project.*` `.tsproj` JSON files in Documents/Transcript Studio; `orig`/`orig_spk` keep the AI output
  - `docx.*` Word export (human corrections = tracked changes) and import (paragraphs carry `ts_turn_<id>` bookmarks)
  - `platform_win.cpp` / `platform_mac.mm` dialogs, paths, open-in-Word
  - `app_main.cpp` webview host; JS calls `window.ts_<name>(...)`, C++ calls `TS.on<Event>(...)`
  - `cli_main.cpp` `ts-cli` for testing/batch
- `ui/` the interface (HTML/CSS/JS, embedded into the program at build time by `cmake/embed_ui.cmake`)
  - `i18n.js` holds every visible string in `en` and `it` — keep both complete
  - design rules: dark #121212, text #EEEEEE, 1px slate (#2a2d33) dividers, no shadows/gradients/glow,
    Playfair Display for titles, JetBrains Mono for timestamps/labels, Inter for body text
- `third_party/NeMo-Speech.cpp` git submodule (NVIDIA, Apache-2.0); only its headers are compiled in
- `models/` downloaded by `scripts/get-models.*` (never committed)

## Build
- Windows: `powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 [-Cuda]` → `dist\Transcript Studio\`
- macOS: `scripts/build-mac.sh` → `dist/Transcript Studio.app` (needs Xcode CLT + Homebrew)
- App only (engine already built): `cmake -S . -B build -G Ninja && cmake --build build`

## Developing
- `TS_UI_DIR=<repo>/ui` loads the interface from disk (edit + reload without rebuilding)
- `TS_ENGINE_DIRS="cuda=<dir>;cpu=<dir>"` and `TS_MODELS=<dir>` point the app at engine/model folders
- `TS_DEBUG=1` enables the web inspector; `TS_AUTOTEST=<file.js>` runs a script after start-up
- `ts-cli <audio> --engine cpu=<dir> --models <dir> --out result.json`

## macOS status
The macOS code (`platform_mac.mm`, `audio_decode_mac.cpp`, `scripts/build-mac.sh`) was written on
Windows and has NOT been compiled on a Mac yet. Expect small fixes on first build. The Metal engine
library is expected at `Contents/Resources/engine/metal/libnemo_speech_asr_c.dylib`; check the actual
file names produced by the `metal-asr` preset and adjust `scripts/build-mac.sh` / `nemo_loader.cpp`.

## Rules
- Recordings and transcripts are confidential research data (often children's voices). Never add
  audio, `.tsproj`, `.docx` or transcript text to the repository, logs or issues.
- Keep the interface simple enough for non-technical users; every new string goes into both languages.
- Keep it light: no new heavy dependencies; single-file libraries via FetchContent are fine.
