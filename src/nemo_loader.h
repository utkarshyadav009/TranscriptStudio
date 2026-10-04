// Loads the NeMo-Speech engine library at run time.
//
// The engine is shipped as several prebuilt "flavours" (cuda, vulkan, metal, cpu),
// each in its own folder with its own ggml backend. Loading one at run time lets a
// single app package use the GPU when the computer has one and fall back to the CPU
// otherwise, without the app itself linking to any GPU runtime.
#pragma once

#include <string>

#include "nemo_speech/asr.h"
#include "nemo_speech/diar.h"

namespace ts {

struct NemoApi {
    std::string flavour;  // "cuda", "vulkan", "metal" or "cpu"
    std::string dir;

#define TS_NEMO_FN(name) decltype(&::name) name = nullptr;
#include "nemo_functions.inc"
#undef TS_NEMO_FN

    NemoApi() = default;
    NemoApi(const NemoApi&) = delete;
    NemoApi& operator=(const NemoApi&) = delete;
    ~NemoApi() { unload(); }

    // Loads the engine library found in `dir`. On failure `err` says why.
    bool load(const std::string& dir, const std::string& flavour, std::string& err);
    void unload();
    bool loaded() const { return lib_ != nullptr; }

   private:
    void* lib_ = nullptr;
};

// File name of the engine library on this platform.
const char* nemo_library_name();

}  // namespace ts
