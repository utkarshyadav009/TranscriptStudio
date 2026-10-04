// ts-cli: run the Transcript Studio pipeline from the command line (testing and batch use).
//
//   ts-cli <audio> [--out result.json] [--device auto|cpu|gpu] [--language auto|it|en|...]
//          [--engine <flavour>=<dir>]... [--models <dir>] [--no-speakers]
//
// Writes words (text, start, end, confidence, speaker) and speaker segments as JSON.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

#include "audio_decode.h"
#include "engine.h"
#include "nlohmann/json.hpp"
#include "platform.h"

using namespace ts;

int main(int argc, char** argv) {
#if defined(_WIN32)
    // UTF-8 arguments on Windows
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    std::vector<std::string> args;
    for (int i = 0; i < wargc; i++) args.push_back(narrow(wargv[i]));
    LocalFree(wargv);
#else
    std::vector<std::string> args(argv, argv + argc);
#endif
    if (args.size() < 2) {
        std::fprintf(stderr,
                     "Usage: ts-cli <audio> [--out result.json] [--device auto|cpu|gpu] [--language auto|it|en]\n"
                     "              [--engine flavour=dir]... [--models dir] [--no-speakers]\n");
        return 1;
    }
    EngineConfig cfg;
    cfg.engine_root = exe_dir();
    std::string models = join_path(exe_dir(), "models"), out_path, language = "auto";
    bool speakers = true;
    for (size_t i = 2; i < args.size(); i++) {
        const std::string& a = args[i];
        auto next = [&]() { return i + 1 < args.size() ? args[++i] : std::string(); };
        if (a == "--out") out_path = next();
        else if (a == "--language") language = next();
        else if (a == "--models") models = next();
        else if (a == "--no-speakers") speakers = false;
        else if (a == "--device") {
            const std::string d = next();
            cfg.device = d == "cpu" ? Device::Cpu : d == "gpu" ? Device::Gpu : Device::Auto;
        } else if (a == "--engine") {
            const std::string v = next();
            const size_t eq = v.find('=');
            if (eq != std::string::npos) cfg.flavours.emplace_back(v.substr(0, eq), v.substr(eq + 1));
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 1;
        }
    }
    cfg.asr_model = join_path(models, "parakeet-tdt-0.6b-v3.q8_0.gguf");
    if (speakers) cfg.diar_model = join_path(models, "Nemotron-3-Diarization.q8_0.gguf");

    std::string err;
    std::vector<float> pcm;
    std::fprintf(stderr, "reading %s\n", args[1].c_str());
    if (!decode_audio(args[1], pcm, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }
    std::fprintf(stderr, "audio: %.1f s\n", pcm.size() / double(kSampleRate));

    Engine engine;
    if (!engine.open(cfg, err)) {
        std::fprintf(stderr, "error: could not start the engine:\n%s\n", err.c_str());
        return 2;
    }
    std::fprintf(stderr, "engine: %s, up to %d speakers\n", engine.device().c_str(), engine.max_speakers());

    Transcript t;
    int last = -1;
    const bool ok = engine.run(pcm, language, [&](const Progress& p) {
        const int pct = (int)(p.fraction * 100);
        if (pct != last) {
            std::fprintf(stderr, "\r%s %3d%%", p.stage == Progress::Speakers ? "speakers    " : "transcribing", pct);
            last = pct;
        }
        return true;
    }, t, err);
    std::fprintf(stderr, "\n");
    if (!ok) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }
    int nspk = 0;
    for (const auto& s : t.segments) nspk = std::max(nspk, s.speaker);
    std::fprintf(stderr, "done: %zu words, %d speakers, language %s | transcription %.1f s (%.1fx real time), "
                         "speakers %.1f s\n",
                 t.words.size(), nspk, t.language.empty() ? "?" : t.language.c_str(), t.seconds_asr,
                 t.duration / std::max(0.01, t.seconds_asr), t.seconds_diar);

    nlohmann::json j;
    j["duration"] = t.duration;
    j["language"] = t.language;
    j["device"] = t.device;
    j["seconds_asr"] = t.seconds_asr;
    j["seconds_diar"] = t.seconds_diar;
    for (const auto& w : t.words) j["words"].push_back({{"w", w.text}, {"s", w.start}, {"e", w.end}, {"c", w.conf}, {"spk", w.speaker}});
    for (const auto& s : t.segments) j["segments"].push_back({{"s", s.start}, {"e", s.end}, {"spk", s.speaker}});
    const std::string text = j.dump(1);
    if (out_path.empty()) {
        std::fwrite(text.data(), 1, text.size(), stdout);
    } else if (!write_file_atomic(out_path, text)) {
        std::fprintf(stderr, "error: could not write %s\n", out_path.c_str());
        return 2;
    }
    return 0;
}
