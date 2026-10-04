// Audio playback with miniaudio. Keeps one output stream open on the system's default
// device (miniaudio follows the default device when it changes, e.g. Bluetooth
// headphones connecting) and plays the project's 16 kHz mono samples.
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace ts {

class Player {
   public:
    Player();
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    bool load(std::vector<float> mono16k, std::string& err);  // replaces any loaded audio
    void unload();
    bool loaded() const { return !pcm_.empty(); }

    void play();
    void pause();
    void seek(double seconds);
    void play_range(double from, double to);  // play then stop at `to`
    void set_speed(double rate);              // 0.5 .. 2.0 (pitch changes with speed)

    double position() const;
    double duration() const;
    bool playing() const { return playing_.load(); }
    const std::vector<float>& samples() const { return pcm_; }

    void fill(float* out, unsigned frames);  // called on the audio thread

   private:
    struct Device;
    std::unique_ptr<Device> dev_;
    std::vector<float> pcm_;
    std::atomic<double> pos_{0};  // in samples
    std::atomic<double> rate_{1.0};
    std::atomic<double> stop_at_{-1};
    std::atomic<bool> playing_{false};
};

}  // namespace ts
