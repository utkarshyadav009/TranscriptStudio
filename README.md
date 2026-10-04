# Transcript Studio

Transcribe any recording on your own computer, tell the speakers apart, and check the result
against the audio — then continue in Microsoft Word with every correction as a tracked change.
Nothing is uploaded anywhere.

- Speech to text: NVIDIA Parakeet TDT 0.6B v3 (25 European languages, detected automatically)
- Speakers: NVIDIA Nemotron 3 Diarization (up to 8 speakers)
- Runs on the CPU, or on the GPU when there is one (NVIDIA CUDA on Windows, Metal on Apple Silicon)
- Interface in English and Italian
- Windows 10/11 and macOS (Apple Silicon)

## Build

Windows (Visual Studio 2022 with C++, CMake, Git; CUDA Toolkit for the GPU flavour):

    powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 -Cuda

macOS (Xcode Command Line Tools, Homebrew):

    scripts/build-mac.sh

The models (about 820 MB) are downloaded by `scripts/get-models.ps1` / `scripts/get-models.sh`.

## Licences

App code: no licence chosen yet (all rights reserved by the author for now).
Third-party components and models: see THIRD_PARTY_NOTICES.txt.
