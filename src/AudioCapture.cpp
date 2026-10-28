#include "AudioCapture.h"
#include "Util.h"

namespace Glow {
namespace {

// PKEY_Device_FriendlyName (defined locally so we don't pull in initguid).
static const PROPERTYKEY kDeviceFriendlyName = {
    { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14
};
static const GUID kSubtypeIEEEFloat = {
    0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }
};
static const GUID kSubtypePCM = {
    0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }
};

std::wstring FriendlyName(IMMDevice* device) {
    ComPtr<IPropertyStore> store;
    if (!device || FAILED(device->OpenPropertyStore(STGM_READ, store.GetAddressOf()))) return L"";
    PROPVARIANT var;
    PropVariantInit(&var);
    std::wstring name;
    if (SUCCEEDED(store->GetValue(kDeviceFriendlyName, &var)) &&
        var.vt == VT_LPWSTR && var.pwszVal) {
        name = var.pwszVal;
    }
    PropVariantClear(&var);
    return name;
}

float ReadSample(const BYTE* p, uint32_t bits, bool isFloat) {
    if (isFloat) {
        if (bits == 64) {
            double v = 0.0;
            std::memcpy(&v, p, sizeof(v));
            return static_cast<float>(v);
        }
        float v = 0.0f;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    if (bits == 16) {
        int16_t v = 0;
        std::memcpy(&v, p, sizeof(v));
        return static_cast<float>(v) / 32768.0f;
    }
    if (bits == 24) {
        int32_t v = static_cast<int32_t>(p[0]) |
                    (static_cast<int32_t>(p[1]) << 8) |
                    (static_cast<int32_t>(p[2]) << 16);
        if (v & 0x00800000) v |= static_cast<int32_t>(0xFF000000);
        return static_cast<float>(v) / 8388608.0f;
    }
    if (bits == 32) {
        int32_t v = 0;
        std::memcpy(&v, p, sizeof(v));
        return static_cast<float>(v) / 2147483648.0f;
    }
    return 0.0f;
}

} // namespace
} // namespace Glow

AudioCapture::AudioCapture() = default;

AudioCapture::~AudioCapture() { Stop(); }

std::wstring AudioCapture::WantedDeviceId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return wantedDeviceId_;
}

void AudioCapture::SetDevice(const std::wstring& id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wantedDeviceId_ = id;
    }
    RequestReinit();
}

std::wstring AudioCapture::DeviceId() const { return WantedDeviceId(); }

std::wstring AudioCapture::Status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void AudioCapture::RequestReinit() {
    reinit_.store(true);
    if (wakeEvent_) SetEvent(wakeEvent_);
}

bool AudioCapture::Start(RingBuffer& ring) {
    if (running_.load()) return true;
    ring_ = &ring;
    stop_.store(false);
    reinit_.store(false);

    wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); // auto reset
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);  // manual reset
    if (!wakeEvent_ || !stopEvent_) {
        Glow::Log(L"[GlowOrb] Failed to create capture events");
        return false;
    }

    running_.store(true);
    try {
        thread_ = std::thread(&AudioCapture::ThreadProc, this);
    } catch (...) {
        running_.store(false);
        Glow::Log(L"[GlowOrb] Failed to start capture thread");
        return false;
    }
    return true;
}

void AudioCapture::Stop() {
    if (!running_.load()) return;
    stop_.store(true);
    if (stopEvent_) SetEvent(stopEvent_);
    if (wakeEvent_) SetEvent(wakeEvent_);
    if (thread_.joinable()) thread_.join();
    running_.store(false);
    if (wakeEvent_) { CloseHandle(wakeEvent_); wakeEvent_ = nullptr; }
    if (stopEvent_) { CloseHandle(stopEvent_); stopEvent_ = nullptr; }
}

bool AudioCapture::OpenStream() {
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(enumerator_.GetAddressOf()));
    if (FAILED(hr)) {
        Glow::Log(L"[GlowOrb] CoCreateInstance(MMDeviceEnumerator): %s", Glow::HrToString(hr).c_str());
        return false;
    }
    enumerator_->RegisterEndpointNotificationCallback(this);

    const std::wstring wanted = WantedDeviceId();
    if (wanted.empty()) {
        hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, device_.GetAddressOf());
    } else {
        hr = enumerator_->GetDevice(wanted.c_str(), device_.GetAddressOf());
        if (FAILED(hr)) {
            hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, device_.GetAddressOf());
        }
    }
    if (FAILED(hr)) {
        Glow::Log(L"[GlowOrb] GetDefaultAudioEndpoint(eRender): %s", Glow::HrToString(hr).c_str());
        return false;
    }

    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(audioClient_.GetAddressOf()));
    if (FAILED(hr)) {
        Glow::Log(L"[GlowOrb] IMMDevice::Activate(IAudioClient): %s", Glow::HrToString(hr).c_str());
        return false;
    }

    WAVEFORMATEX* mix = nullptr;
    hr = audioClient_->GetMixFormat(&mix);
    if (FAILED(hr) || !mix) {
        Glow::Log(L"[GlowOrb] GetMixFormat: %s", Glow::HrToString(hr).c_str());
        return false;
    }

    floatFormat_ = (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix);
        floatFormat_ = IsEqualGUID(ext->SubFormat, Glow::kSubtypeIEEEFloat);
        if (!floatFormat_ && !IsEqualGUID(ext->SubFormat, Glow::kSubtypePCM)) {
            Glow::Log(L"[GlowOrb] Unsupported extensible subtype, assuming PCM");
        }
    }
    channels_ = mix->nChannels ? mix->nChannels : 2;
    bits_ = mix->wBitsPerSample ? mix->wBitsPerSample : 32;
    blockAlign_ = mix->nBlockAlign ? mix->nBlockAlign : (channels_ * bits_ / 8);
    sampleRate_.store(static_cast<float>(mix->nSamplesPerSec));

    // 100 ms shared-mode buffer. Timer-driven (no EVENTCALLBACK) so loopback
    // works on every Windows version from 7 through 11.
    const REFERENCE_TIME bufferDuration = 1000000; // 100 ns units => 100 ms
    hr = audioClient_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                  AUDCLNT_STREAMFLAGS_LOOPBACK,
                                  bufferDuration, 0, mix, nullptr);
    CoTaskMemFree(mix);
    if (FAILED(hr)) {
        Glow::Log(L"[GlowOrb] IAudioClient::Initialize(loopback): %s", Glow::HrToString(hr).c_str());
        return false;
    }

    REFERENCE_TIME defaultPeriod = 0, minimumPeriod = 0;
    if (SUCCEEDED(audioClient_->GetDevicePeriod(&defaultPeriod, &minimumPeriod))) {
        sleepMs_ = std::max(1u, static_cast<uint32_t>(defaultPeriod / 10000) / 2);
        sleepMs_ = std::min(sleepMs_, 10u);
    } else {
        sleepMs_ = 5;
    }

    hr = audioClient_->GetService(__uuidof(IAudioCaptureClient),
                                  reinterpret_cast<void**>(captureClient_.GetAddressOf()));
    if (FAILED(hr)) {
        Glow::Log(L"[GlowOrb] GetService(IAudioCaptureClient): %s", Glow::HrToString(hr).c_str());
        return false;
    }
    hr = audioClient_->GetBufferSize(&bufferFrames_);
    if (FAILED(hr)) bufferFrames_ = 4800;

    hr = audioClient_->Start();
    if (FAILED(hr)) {
        Glow::Log(L"[GlowOrb] IAudioClient::Start: %s", Glow::HrToString(hr).c_str());
        return false;
    }

    std::wstring name = Glow::FriendlyName(device_.Get());
    if (name.empty()) name = wanted.empty() ? L"Default output device" : wanted;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = name + L" | " + std::to_wstring(static_cast<int>(sampleRate_.load())) +
                  L" Hz | " + std::to_wstring(channels_) + L"ch | " +
                  (floatFormat_ ? L"float" : L"pcm") + std::to_wstring(bits_);
    }
    Glow::Log(L"[GlowOrb] Loopback active: %s", status_.c_str());
    return true;
}

void AudioCapture::CloseStream() {
    if (audioClient_) audioClient_->Stop();
    if (enumerator_) enumerator_->UnregisterEndpointNotificationCallback(this);
    captureClient_.Reset();
    audioClient_.Reset();
    device_.Reset();
    enumerator_.Reset();
}

void AudioCapture::ConvertAndWrite(const BYTE* data, uint32_t frames) {
    if (!ring_ || !data || frames == 0) return;
    mono_.resize(frames);
    const uint32_t sampleBytes = std::max(1u, bits_ / 8);
    for (uint32_t f = 0; f < frames; ++f) {
        float sum = 0.0f;
        const BYTE* frame = data + static_cast<size_t>(f) * blockAlign_;
        for (uint32_t c = 0; c < channels_; ++c) {
            sum += Glow::ReadSample(frame + static_cast<size_t>(c) * sampleBytes,
                                    bits_, floatFormat_);
        }
        mono_[f] = sum / static_cast<float>(channels_);
    }
    ring_->Write(mono_.data(), frames);
}

void AudioCapture::ThreadProc() {
    HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
    const bool comOk = SUCCEEDED(comHr);
    if (!comOk) {
        Glow::Log(L"[GlowOrb] Capture thread CoInitializeEx failed: %s",
                  Glow::HrToString(comHr).c_str());
    }

    if (comOk) OpenStream();

    HANDLE handles[2] = { stopEvent_, wakeEvent_ };
    while (!stop_.load()) {
        if (reinit_.exchange(false) || !captureClient_) {
            CloseStream();
            if (comOk && !OpenStream()) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    status_ = L"Waiting for audio device...";
                }
                WaitForMultipleObjects(2, handles, FALSE, 400);
                continue;
            }
        }

        UINT32 packet = 0;
        HRESULT hr = captureClient_->GetNextPacketSize(&packet);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED || FAILED(hr)) {
            RequestReinit();
            WaitForMultipleObjects(2, handles, FALSE, sleepMs_);
            continue;
        }

        while (packet > 0 && !stop_.load()) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 devicePosition = 0, qpcPosition = 0;
            hr = captureClient_->GetBuffer(&data, &frames, &flags,
                                           &devicePosition, &qpcPosition);
            if (hr == AUDCLNT_S_BUFFER_EMPTY) break;
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                RequestReinit();
                break;
            }
            if (FAILED(hr)) {
                RequestReinit();
                break;
            }
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                if (ring_) ring_->WriteZeros(frames);
            } else {
                ConvertAndWrite(data, frames);
            }
            captureClient_->ReleaseBuffer(frames);
            if (FAILED(captureClient_->GetNextPacketSize(&packet))) break;
        }

        DWORD result = WaitForMultipleObjects(2, handles, FALSE, sleepMs_);
        if (result == WAIT_OBJECT_0) break;
    }

    CloseStream();
    if (comOk) CoUninitialize();
}

std::vector<std::pair<std::wstring, std::wstring>> AudioCapture::EnumDevices() {
    std::vector<std::pair<std::wstring, std::wstring>> out;
    ComPtr<IMMDeviceEnumerator> en;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(en.GetAddressOf()));
    if (FAILED(hr)) return out;

    ComPtr<IMMDeviceCollection> collection;
    hr = en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, collection.GetAddressOf());
    if (FAILED(hr)) return out;

    UINT count = 0;
    collection->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> dev;
        if (FAILED(collection->Item(i, dev.GetAddressOf()))) continue;
        LPWSTR id = nullptr;
        if (FAILED(dev->GetId(&id)) || !id) continue;
        std::wstring name = Glow::FriendlyName(dev.Get());
        if (name.empty()) name = std::wstring(id);
        out.emplace_back(std::wstring(id), name);
        CoTaskMemFree(id);
    }
    return out;
}

// ---------------------------------------------------------------------------
// IUnknown / IMMNotificationClient
// ---------------------------------------------------------------------------
STDMETHODIMP AudioCapture::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, __uuidof(IMMNotificationClient))) {
        *ppv = static_cast<IMMNotificationClient*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) AudioCapture::AddRef() {
    return static_cast<ULONG>(++refCount_);
}

STDMETHODIMP_(ULONG) AudioCapture::Release() {
    return static_cast<ULONG>(--refCount_);
}

STDMETHODIMP AudioCapture::OnDeviceStateChanged(LPCWSTR, DWORD) {
    RequestReinit();
    return S_OK;
}

STDMETHODIMP AudioCapture::OnDeviceAdded(LPCWSTR) {
    RequestReinit();
    return S_OK;
}

STDMETHODIMP AudioCapture::OnDeviceRemoved(LPCWSTR) {
    RequestReinit();
    return S_OK;
}

STDMETHODIMP AudioCapture::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) {
    if (flow == eRender && role == eConsole && WantedDeviceId().empty()) RequestReinit();
    return S_OK;
}

STDMETHODIMP AudioCapture::OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) {
    return S_OK;
}
