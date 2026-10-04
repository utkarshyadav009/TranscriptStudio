#include "nemo_loader.h"

#include "platform.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace ts {

const char* nemo_library_name() {
#if defined(_WIN32)
    return "nemo_speech_asr_c.dll";
#elif defined(__APPLE__)
    return "libnemo_speech_asr_c.dylib";
#else
    return "libnemo_speech_asr_c.so";
#endif
}

bool NemoApi::load(const std::string& dir_, const std::string& flavour_, std::string& err) {
    unload();
    const std::string path = join_path(dir_, nemo_library_name());
    if (!file_exists(path)) {
        err = "engine library not found: " + path;
        return false;
    }
#if defined(_WIN32)
    // Search the library's own folder first, so it finds the ggml backend DLLs
    // that belong to this flavour rather than another one.
    HMODULE h = LoadLibraryExW(widen(path).c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!h) {
        err = "could not load " + path + " (Windows error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    lib_ = h;
    auto sym = [&](const char* name) { return reinterpret_cast<void*>(GetProcAddress(h, name)); };
#else
    void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        const char* e = dlerror();
        err = "could not load " + path + (e ? std::string(": ") + e : std::string());
        return false;
    }
    lib_ = h;
    auto sym = [&](const char* name) { return dlsym(h, name); };
#endif
#define TS_NEMO_FN(name)                                                    \
    name = reinterpret_cast<decltype(name)>(sym(#name));                    \
    if (!name) {                                                            \
        err = std::string("engine library is missing ") + #name + " (" + path + ")"; \
        unload();                                                           \
        return false;                                                       \
    }
#include "nemo_functions.inc"
#undef TS_NEMO_FN
    dir = dir_;
    flavour = flavour_;
    return true;
}

void NemoApi::unload() {
    if (!lib_) return;
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(lib_));
#else
    dlclose(lib_);
#endif
    lib_ = nullptr;
#define TS_NEMO_FN(name) name = nullptr;
#include "nemo_functions.inc"
#undef TS_NEMO_FN
    flavour.clear();
    dir.clear();
}

}  // namespace ts
