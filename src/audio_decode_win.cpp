// Windows: decode anything Media Foundation understands (M4A/AAC, MP4/MOV, WMA, ...).
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include "audio_decode.h"
#include "platform.h"

namespace ts {

namespace {
template <class T>
void release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}
}  // namespace

bool decode_with_platform(const std::string& path, std::vector<float>& out, int& rate, int& channels,
                          std::string& err) {
    out.clear();
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(MFStartup(MF_VERSION))) {
        err = "Windows Media Foundation is not available";
        if (SUCCEEDED(co)) CoUninitialize();
        return false;
    }
    IMFSourceReader* reader = nullptr;
    IMFMediaType* want = nullptr;
    IMFMediaType* got = nullptr;
    bool ok = false;
    do {
        if (FAILED(MFCreateSourceReaderFromURL(widen(path).c_str(), nullptr, &reader))) {
            err = "this file type can't be read";
            break;
        }
        reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
        if (FAILED(reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE))) {
            err = "the file has no audio track";
            break;
        }
        MFCreateMediaType(&want);
        want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
        if (FAILED(reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, want))) {
            err = "the audio in this file can't be decoded";
            break;
        }
        reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &got);
        UINT32 ch = 0, sr = 0;
        got->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
        got->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
        channels = (int)ch;
        rate = (int)sr;
        for (;;) {
            DWORD flags = 0;
            IMFSample* sample = nullptr;
            if (FAILED(reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr,
                                          &sample)))
                break;
            if (sample) {
                IMFMediaBuffer* buf = nullptr;
                if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf))) {
                    BYTE* data = nullptr;
                    DWORD len = 0;
                    if (SUCCEEDED(buf->Lock(&data, nullptr, &len))) {
                        const float* f = reinterpret_cast<const float*>(data);
                        out.insert(out.end(), f, f + len / sizeof(float));
                        buf->Unlock();
                    }
                    buf->Release();
                }
                sample->Release();
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        }
        ok = !out.empty() && rate > 0 && channels > 0;
        if (!ok) err = "no audio could be decoded from this file";
    } while (false);
    release(got);
    release(want);
    release(reader);
    MFShutdown();
    if (SUCCEEDED(co)) CoUninitialize();
    return ok;
}

}  // namespace ts
