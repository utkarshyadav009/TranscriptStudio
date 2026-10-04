// Reads any recording into mono float samples at 16 kHz.
//   WAV / MP3 / FLAC: miniaudio (all platforms)
//   everything else (M4A, AAC, MP4/MOV video, WMA...): the operating system's own
//   decoder - Media Foundation on Windows, AudioToolbox on macOS.
#pragma once

#include <string>
#include <vector>

namespace ts {

constexpr int kSampleRate = 16000;

bool decode_audio(const std::string& path, std::vector<float>& mono16k, std::string& err);

// Implemented per platform (audio_decode_win.cpp / audio_decode_mac.cpp).
// Returns interleaved float samples with their own rate and channel count.
bool decode_with_platform(const std::string& path, std::vector<float>& interleaved, int& rate, int& channels,
                          std::string& err);

// Downmix to mono and resample to 16 kHz.
void to_mono_16k(const std::vector<float>& interleaved, int rate, int channels, std::vector<float>& out);

// (min, max) of the samples in each of `columns` equal slices of [t0, t1) seconds.
std::vector<std::pair<float, float>> peaks(const std::vector<float>& mono16k, double t0, double t1, int columns);

}  // namespace ts
