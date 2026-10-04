#include "engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "nemo_loader.h"
#include "platform.h"

namespace ts {

namespace {
constexpr int kRate = 16000;

std::string flavour_label(const std::string& f) {
    if (f == "cuda") return "NVIDIA GPU (CUDA)";
    if (f == "vulkan") return "GPU (Vulkan)";
    if (f == "metal") return "Apple GPU (Metal)";
    return "CPU";
}

double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
}  // namespace

struct Engine::Impl {
    NemoApi api;
    nemo_speech_asr_recognizer* asr = nullptr;
    nemo_speech_diar_model* diar = nullptr;

    void close() {
        if (diar) api.nemo_speech_diar_destroy(diar);
        if (asr) api.nemo_speech_asr_destroy(asr);
        diar = nullptr;
        asr = nullptr;
        api.unload();
    }
    ~Impl() { close(); }

    std::string last_error() const {
        const char* e = api.loaded() ? api.nemo_speech_asr_last_error() : nullptr;
        return e && *e ? e : "unknown engine error";
    }

    bool try_flavour(const std::string& name, const std::string& dir, const EngineConfig& cfg, std::string& err) {
        if (!api.load(dir, name, err)) return false;
        const int gpu = name == "cpu" ? -1 : 0;

        nemo_speech_asr_backend_config backend{};
        backend.size = sizeof(backend);
        backend.gpu = gpu;
        nemo_speech_asr_model_config model{};
        model.size = sizeof(model);
        model.path = cfg.asr_model.c_str();
        nemo_speech_asr_recognizer_config rc{};
        rc.size = sizeof(rc);
        rc.backend = &backend;
        rc.model = &model;
        if (api.nemo_speech_asr_create(&rc, &asr) != NEMO_SPEECH_ASR_OK) {
            err = flavour_label(name) + ": " + last_error();
            close();
            return false;
        }
        if (!cfg.diar_model.empty()) {
            nemo_speech_diar_model_config dc{};
            dc.size = sizeof(dc);
            dc.model_path = cfg.diar_model.c_str();
            dc.gpu = gpu;
            // Whole files, not live audio: the offline geometry uses ~21 s chunks instead of
            // ~1 s low-latency ones, which is about 14x less work for the same model.
            dc.preset = cfg.diar_preset.empty() ? nullptr : cfg.diar_preset.c_str();
            if (api.nemo_speech_diar_create(&dc, &diar) != NEMO_SPEECH_ASR_OK) {
                err = flavour_label(name) + " (speakers): " + last_error();
                close();
                return false;
            }
        }
        return true;
    }
};

Engine::Engine() : d_(new Impl) {}
Engine::~Engine() = default;
bool Engine::is_open() const { return d_->asr != nullptr; }
std::string Engine::device() const { return is_open() ? flavour_label(d_->api.flavour) : ""; }
int Engine::max_speakers() const { return d_->diar ? d_->api.nemo_speech_diar_num_speakers(d_->diar) : 0; }

bool Engine::open(const EngineConfig& cfg, std::string& err) {
    d_->close();
    if (!file_exists(cfg.asr_model)) {
        err = "speech model not found: " + cfg.asr_model;
        return false;
    }
    if (!cfg.diar_model.empty() && !file_exists(cfg.diar_model)) {
        err = "speaker model not found: " + cfg.diar_model;
        return false;
    }

    std::vector<std::pair<std::string, std::string>> order = cfg.flavours;
    if (order.empty()) {
        std::vector<std::string> names;
#if defined(__APPLE__)
        names = {"metal", "cpu"};
#else
        if (has_nvidia_driver()) names.push_back("cuda");
        if (has_vulkan_loader()) names.push_back("vulkan");
        names.push_back("cpu");
#endif
        for (const auto& n : names) order.emplace_back(n, join_path(join_path(cfg.engine_root, "engine"), n));
    }
    if (cfg.device == Device::Cpu)
        order.erase(std::remove_if(order.begin(), order.end(), [](auto& f) { return f.first != "cpu"; }), order.end());
    if (cfg.device == Device::Gpu)
        order.erase(std::remove_if(order.begin(), order.end(), [](auto& f) { return f.first == "cpu"; }), order.end());

    std::string tried;
    for (const auto& [name, dir] : order) {
        std::string e;
        if (d_->try_flavour(name, dir, cfg, e)) return true;
        tried += (tried.empty() ? "" : "\n") + e;
    }
    err = tried.empty() ? "no speech engine found" : tried;
    return false;
}

std::vector<std::pair<size_t, size_t>> split_at_pauses(const std::vector<float>& pcm, int sr, double min_s,
                                                       double max_s) {
    std::vector<std::pair<size_t, size_t>> out;
    const size_t hop = sr / 100;  // 10 ms
    const size_t nfr = pcm.size() / hop;
    // loudness per 10 ms, smoothed over 300 ms
    std::vector<float> e(nfr), sm(nfr);
    for (size_t f = 0; f < nfr; f++) {
        double s = 0;
        for (size_t i = 0; i < hop; i++) s += pcm[f * hop + i] * pcm[f * hop + i];
        e[f] = static_cast<float>(s / hop);
    }
    double run = 0;
    const size_t w = 30;
    for (size_t f = 0; f < nfr; f++) {
        run += e[f];
        if (f >= w) run -= e[f - w];
        sm[f] = static_cast<float>(run);
    }
    const size_t minf = static_cast<size_t>(min_s * 100), maxf = static_cast<size_t>(max_s * 100);
    size_t pos = 0;
    while (nfr - pos > maxf) {
        size_t best = pos + minf;
        for (size_t f = pos + minf; f < pos + maxf; f++)
            if (sm[f] < sm[best]) best = f;
        const size_t cut = best > w / 2 ? best - w / 2 : best;  // centre of the quiet window
        out.emplace_back(pos * hop, cut * hop);
        pos = cut;
    }
    out.emplace_back(pos * hop, pcm.size());
    return out;
}

void assign_speakers(std::vector<Word>& words, const std::vector<SpeakerSegment>& segs) {
    size_t lo = 0;
    for (auto& w : words) {
        while (lo < segs.size() && segs[lo].end < w.start - 30) lo++;  // segments are sorted by start
        double best = 0, nearest = 1e9;
        int best_spk = 0, near_spk = 0;
        for (size_t i = lo; i < segs.size() && segs[i].start <= w.end + 30; i++) {
            const auto& s = segs[i];
            const double ov = std::min(w.end, s.end) - std::max(w.start, s.start);
            if (ov > best) best = ov, best_spk = s.speaker;
            const double gap = std::max(s.start - w.end, w.start - s.end);
            if (gap < nearest) nearest = gap, near_spk = s.speaker;
        }
        w.speaker = best_spk ? best_spk : (nearest <= 1.0 ? near_spk : 0);
    }
}

bool Engine::run(const std::vector<float>& pcm, const std::string& language, const ProgressFn& progress,
                 Transcript& out, std::string& err) {
    if (!is_open()) {
        err = "engine not open";
        return false;
    }
    auto& api = d_->api;
    out = Transcript{};
    out.duration = pcm.size() / static_cast<double>(kRate);
    out.device = device();
    const bool speakers = d_->diar != nullptr;
    const double asr_share = speakers ? 0.75 : 1.0;  // rough share of the total time
    auto report = [&](Progress::Stage st, double f) { return !progress || progress({st, f}); };

    // 1. transcription, piece by piece (Parakeet TDT is a whole-utterance model)
    const double t0 = now_s();
    const auto pieces = split_at_pauses(pcm, kRate, 40.0, 70.0);
    nemo_speech_asr_recognition_options opts = api.nemo_speech_asr_recognition_options_default();
    opts.enable_word_time_offsets = true;
    opts.enable_automatic_punctuation = true;
    const bool auto_lang = language.empty() || language == "auto";
    opts.language_code = auto_lang ? nullptr : language.c_str();
    std::vector<std::pair<std::string, double>> langs;  // detected language per piece, weighted by length
    for (size_t p = 0; p < pieces.size(); p++) {
        if (!report(Progress::Transcribing, asr_share * p / pieces.size())) {
            err = "cancelled";
            return false;
        }
        const auto [a, b] = pieces[p];
        nemo_speech_asr_result* r = nullptr;
        if (api.nemo_speech_asr_recognize_f32(d_->asr, &opts, pcm.data() + a, b - a, kRate, &r) != NEMO_SPEECH_ASR_OK) {
            err = "transcription failed: " + d_->last_error();
            return false;
        }
        if (r && api.nemo_speech_asr_result_alternative_count(r) > 0) {
            const double off = a / static_cast<double>(kRate);
            const size_t n = api.nemo_speech_asr_result_word_count(r, 0);
            for (size_t i = 0; i < n; i++) {
                Word w;
                const char* t = api.nemo_speech_asr_result_word_text(r, 0, i);
                w.text = t ? t : "";
                w.start = off + api.nemo_speech_asr_result_word_start_time(r, 0, i) / 1000.0;
                w.end = off + api.nemo_speech_asr_result_word_end_time(r, 0, i) / 1000.0;
                w.conf = api.nemo_speech_asr_result_word_confidence(r, 0, i);
                if (!w.text.empty()) out.words.push_back(std::move(w));
            }
            if (api.nemo_speech_asr_result_language_count(r, 0) > 0) {
                const char* lc = api.nemo_speech_asr_result_language_code(r, 0, 0);
                if (lc && *lc) langs.emplace_back(lc, double(b - a));
            }
        }
        if (r) api.nemo_speech_asr_result_destroy(r);
    }
    out.seconds_asr = now_s() - t0;
    if (!auto_lang) {
        out.language = language;
    } else if (!langs.empty()) {
        std::sort(langs.begin(), langs.end());
        std::string best;
        double best_w = -1;
        for (size_t i = 0; i < langs.size();) {
            size_t j = i;
            double wsum = 0;
            while (j < langs.size() && langs[j].first == langs[i].first) wsum += langs[j++].second;
            if (wsum > best_w) best_w = wsum, best = langs[i].first;
            i = j;
        }
        out.language = best;
    }

    // 2. speakers: one stream over the whole recording, so labels stay consistent
    if (speakers) {
        const double t1 = now_s();
        nemo_speech_diar_stream* job = nullptr;
        if (api.nemo_speech_diar_stream_open(d_->diar, &job) != NEMO_SPEECH_ASR_OK) {
            err = "speaker separation failed: " + d_->last_error();
            return false;
        }
        const size_t slice = kRate * 2;
        for (size_t off = 0; off < pcm.size(); off += slice) {
            if (!report(Progress::Speakers, asr_share + (1 - asr_share) * off / pcm.size())) {
                api.nemo_speech_diar_stream_close(job);
                err = "cancelled";
                return false;
            }
            const size_t n = std::min(slice, pcm.size() - off);
            if (api.nemo_speech_diar_stream_push_f32(job, pcm.data() + off, n, kRate) != NEMO_SPEECH_ASR_OK) {
                err = "speaker separation failed: " + d_->last_error();
                api.nemo_speech_diar_stream_close(job);
                return false;
            }
        }
        size_t count = 0;
        bool ok = api.nemo_speech_diar_stream_finish(job) == NEMO_SPEECH_ASR_OK &&
                  api.nemo_speech_diar_segments(job, nullptr, nullptr, 0, &count) == NEMO_SPEECH_ASR_OK;
        std::vector<nemo_speech_diar_segment> segs(count);
        if (ok && count)
            ok = api.nemo_speech_diar_segments(job, nullptr, segs.data(), segs.size(), &count) == NEMO_SPEECH_ASR_OK;
        api.nemo_speech_diar_stream_close(job);
        if (!ok) {
            err = "speaker separation failed: " + d_->last_error();
            return false;
        }
        for (size_t i = 0; i < count; i++) out.segments.push_back({segs[i].start_time, segs[i].end_time, segs[i].speaker});
        assign_speakers(out.words, out.segments);
        out.seconds_diar = now_s() - t1;
    }
    report(Progress::Done, 1.0);
    return true;
}

}  // namespace ts
