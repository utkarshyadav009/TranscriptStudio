#include "player.h"

#include <algorithm>
#include <string>

#include "audio_decode.h"
#include "miniaudio.h"

namespace ts {

struct Player::Device {
    ma_device dev{};
    bool ok = false;
};

static void data_callback(ma_device* d, void* out, const void*, ma_uint32 frames) {
    static_cast<Player*>(d->pUserData)->fill(static_cast<float*>(out), frames);
}

Player::Player() : dev_(new Device) {}
Player::~Player() { unload(); }

bool Player::load(std::vector<float> mono16k, std::string& err) {
    unload();
    pcm_ = std::move(mono16k);
    pos_ = 0;
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 1;
    cfg.sampleRate = kSampleRate;  // miniaudio converts to the device's own rate
    cfg.dataCallback = data_callback;
    cfg.pUserData = this;
    if (ma_device_init(nullptr, &cfg, &dev_->dev) != MA_SUCCESS) {
        err = "no sound output device is available";
        return false;
    }
    dev_->ok = true;
    if (ma_device_start(&dev_->dev) != MA_SUCCESS) {
        err = "the sound output could not be started";
        unload();
        return false;
    }
    return true;
}

void Player::unload() {
    playing_ = false;
    if (dev_->ok) {
        ma_device_uninit(&dev_->dev);
        dev_->ok = false;
    }
    pcm_.clear();
    pos_ = 0;
}

void Player::play() {
    if (pcm_.empty()) return;
    if (pos_ >= pcm_.size() - 1) pos_ = 0;
    stop_at_ = -1;
    playing_ = true;
}

void Player::pause() { playing_ = false; }

void Player::seek(double s) {
    pos_ = std::clamp(s * kSampleRate, 0.0, pcm_.empty() ? 0.0 : double(pcm_.size() - 1));
}

void Player::play_range(double from, double to) {
    seek(from);
    playing_ = true;
    stop_at_ = to * kSampleRate;
}

void Player::set_speed(double r) { rate_ = std::clamp(r, 0.5, 2.0); }
double Player::position() const { return pos_ / kSampleRate; }
double Player::duration() const { return pcm_.size() / double(kSampleRate); }

void Player::fill(float* out, unsigned frames) {
    if (!playing_ || pcm_.empty()) {
        std::fill(out, out + frames, 0.0f);
        return;
    }
    double p = pos_;
    const double r = rate_, stop = stop_at_;
    const double last = double(pcm_.size() - 1);
    for (unsigned i = 0; i < frames; i++) {
        if (p >= last || (stop >= 0 && p >= stop)) {
            std::fill(out + i, out + frames, 0.0f);
            playing_ = false;
            break;
        }
        const size_t k = (size_t)p;
        const float f = float(p - k);
        out[i] = pcm_[k] + (pcm_[k + 1] - pcm_[k]) * f;  // linear interpolation for speed changes
        p += r;
    }
    pos_ = std::min(p, last);
}

}  // namespace ts
