#include "audio_decode.h"

#include <algorithm>

#include "miniaudio.h"
#include "platform.h"

namespace ts {

bool decode_audio(const std::string& path, std::vector<float>& out, std::string& err) {
    out.clear();
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, kSampleRate);
    ma_decoder dec;
#if defined(_WIN32)
    const ma_result r = ma_decoder_init_file_w(widen(path).c_str(), &cfg, &dec);
#else
    const ma_result r = ma_decoder_init_file(path.c_str(), &cfg, &dec);
#endif
    if (r == MA_SUCCESS) {
        std::vector<float> buf(kSampleRate * 4);
        for (;;) {
            ma_uint64 got = 0;
            ma_decoder_read_pcm_frames(&dec, buf.data(), buf.size(), &got);
            if (got == 0) break;
            out.insert(out.end(), buf.begin(), buf.begin() + (size_t)got);
        }
        ma_decoder_uninit(&dec);
        if (!out.empty()) return true;
    }
    std::vector<float> inter;
    int rate = 0, ch = 0;
    if (!decode_with_platform(path, inter, rate, ch, err)) return false;
    to_mono_16k(inter, rate, ch, out);
    if (out.empty()) {
        err = "the file contains no audio";
        return false;
    }
    return true;
}

void to_mono_16k(const std::vector<float>& in, int rate, int ch, std::vector<float>& out) {
    out.clear();
    if (in.empty() || rate <= 0 || ch <= 0) return;
    const size_t frames = in.size() / ch;
    std::vector<float> mono(frames);
    for (size_t f = 0; f < frames; f++) {
        float s = 0;
        for (int c = 0; c < ch; c++) s += in[f * ch + c];
        mono[f] = s / ch;
    }
    if (rate == kSampleRate) {
        out.swap(mono);
        return;
    }
    ma_resampler_config rc = ma_resampler_config_init(ma_format_f32, 1, (ma_uint32)rate, kSampleRate,
                                                      ma_resample_algorithm_linear);
    rc.linear.lpfOrder = 8;  // filter out what can't be represented at 16 kHz
    ma_resampler rs;
    if (ma_resampler_init(&rc, nullptr, &rs) != MA_SUCCESS) return;
    ma_uint64 expect = 0;
    ma_resampler_get_expected_output_frame_count(&rs, frames, &expect);
    out.resize((size_t)expect + 16);
    ma_uint64 in_n = frames, out_n = out.size();
    ma_resampler_process_pcm_frames(&rs, mono.data(), &in_n, out.data(), &out_n);
    out.resize((size_t)out_n);
    ma_resampler_uninit(&rs, nullptr);
}

std::vector<std::pair<float, float>> peaks(const std::vector<float>& x, double t0, double t1, int cols) {
    std::vector<std::pair<float, float>> out(std::max(cols, 0), {0.f, 0.f});
    if (cols <= 0 || x.empty() || t1 <= t0) return out;
    const double step = (t1 - t0) * kSampleRate / cols;
    for (int c = 0; c < cols; c++) {
        const double a = t0 * kSampleRate + c * step;
        long long i0 = (long long)a, i1 = (long long)(a + step);
        if (i1 <= i0) i1 = i0 + 1;
        i0 = std::max(0LL, i0);
        i1 = std::min((long long)x.size(), i1);
        if (i0 >= i1) continue;
        float lo = x[i0], hi = x[i0];
        for (long long i = i0; i < i1; i++) lo = std::min(lo, x[i]), hi = std::max(hi, x[i]);
        out[c] = {lo, hi};
    }
    return out;
}

}  // namespace ts
