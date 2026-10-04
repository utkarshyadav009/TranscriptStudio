// macOS: decode anything AudioToolbox understands (M4A/AAC, MP4/MOV, AIFF, CAF, ...).
// ExtAudioFile converts to the requested client format, so this returns mono 16 kHz.
#include <AudioToolbox/AudioToolbox.h>

#include "audio_decode.h"

namespace ts {

bool decode_with_platform(const std::string& path, std::vector<float>& out, int& rate, int& channels,
                          std::string& err) {
    out.clear();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.c_str()),
                                                           (CFIndex)path.size(), false);
    ExtAudioFileRef file = nullptr;
    const OSStatus st = ExtAudioFileOpenURL(url, &file);
    CFRelease(url);
    if (st != noErr || !file) {
        err = "this file type can't be read";
        return false;
    }
    AudioStreamBasicDescription client = {};
    client.mSampleRate = kSampleRate;
    client.mFormatID = kAudioFormatLinearPCM;
    client.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    client.mBytesPerPacket = sizeof(float);
    client.mFramesPerPacket = 1;
    client.mBytesPerFrame = sizeof(float);
    client.mChannelsPerFrame = 1;
    client.mBitsPerChannel = 32;
    if (ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat, sizeof(client), &client) != noErr) {
        ExtAudioFileDispose(file);
        err = "the audio in this file can't be decoded";
        return false;
    }
    std::vector<float> buf(kSampleRate);
    for (;;) {
        AudioBufferList list;
        list.mNumberBuffers = 1;
        list.mBuffers[0].mNumberChannels = 1;
        list.mBuffers[0].mDataByteSize = (UInt32)(buf.size() * sizeof(float));
        list.mBuffers[0].mData = buf.data();
        UInt32 frames = (UInt32)buf.size();
        if (ExtAudioFileRead(file, &frames, &list) != noErr || frames == 0) break;
        out.insert(out.end(), buf.begin(), buf.begin() + frames);
    }
    ExtAudioFileDispose(file);
    rate = kSampleRate;
    channels = 1;
    if (out.empty()) {
        err = "no audio could be decoded from this file";
        return false;
    }
    return true;
}

}  // namespace ts
