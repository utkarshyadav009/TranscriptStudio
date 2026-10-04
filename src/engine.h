// Speech pipeline: transcription (Parakeet TDT v3) + speaker separation (Nemotron 3
// Diarization) through NeMo-Speech, then every word is given a speaker.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ts {

struct Word {
    std::string text;
    double start = 0, end = 0;  // seconds
    float conf = 1.0f;          // 0..1 from the recogniser
    int speaker = 0;            // 1-based; 0 = unknown
};

struct SpeakerSegment {
    double start = 0, end = 0;
    int speaker = 0;
};

struct Transcript {
    std::vector<Word> words;
    std::vector<SpeakerSegment> segments;
    std::string language;  // detected or requested, e.g. "it"
    std::string device;    // human readable, e.g. "NVIDIA GPU (CUDA)"
    double duration = 0;
    double seconds_asr = 0, seconds_diar = 0;
};

enum class Device { Auto, Cpu, Gpu };

struct EngineConfig {
    // Engine flavours to try, in order: {"cuda", dir}, {"cpu", dir}, ...
    // Leave empty to discover <engine_root>/engine/<flavour>/ automatically.
    std::vector<std::pair<std::string, std::string>> flavours;
    std::string engine_root;
    std::string asr_model;   // ASR GGUF path
    std::string diar_model;  // diarization GGUF path; "" = no speaker separation
    std::string diar_preset = "v3-offline";  // NeMo geometry preset; "" = library default (low latency)
    Device device = Device::Auto;
};

struct Progress {
    enum Stage { Loading, Transcribing, Speakers, Done } stage = Loading;
    double fraction = 0;  // 0..1 within the whole job
};
// Return false to cancel.
using ProgressFn = std::function<bool(const Progress&)>;

class Engine {
   public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Loads the best available engine flavour and both models.
    bool open(const EngineConfig& cfg, std::string& err);
    bool is_open() const;
    std::string device() const;
    int max_speakers() const;

    // `pcm` is mono float at 16 kHz. `language` is "" / "auto" or a code such as "it".
    bool run(const std::vector<float>& pcm, const std::string& language, const ProgressFn& progress,
             Transcript& out, std::string& err);

   private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Pieces of at most `max_s` seconds, cut at the quietest moment after `min_s` seconds.
std::vector<std::pair<size_t, size_t>> split_at_pauses(const std::vector<float>& pcm, int sample_rate,
                                                       double min_s, double max_s);

// Gives each word the speaker whose segments overlap it most (nearest segment within 1 s otherwise).
void assign_speakers(std::vector<Word>& words, const std::vector<SpeakerSegment>& segments);

}  // namespace ts
