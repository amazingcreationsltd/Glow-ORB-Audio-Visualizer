#pragma once
#include "Common.h"
#include "RingBuffer.h"

// WASAPI loopback capture of the default (or a chosen) render endpoint.
// Captures the *system mix* - Spotify, YouTube, VLC, FL Studio, games, etc.
// Timer-driven (no EVENTCALLBACK) because event-driven loopback is broken on
// Windows 7/8. Recovers automatically when the default device changes or the
// stream is invalidated by a format change.
class AudioCapture : public IMMNotificationClient {
public:
    AudioCapture();
    ~AudioCapture();

    bool Start(RingBuffer& ring);
    void Stop();

    void SetDevice(const std::wstring& id);   // empty = follow default
    std::wstring DeviceId() const;

    float SampleRate() const { return sampleRate_.load(); }
    std::wstring Status() const;
    bool Running() const { return running_.load(); }

    // (device id, friendly name) for every active render endpoint.
    static std::vector<std::pair<std::wstring, std::wstring>> EnumDevices();

    // --- IUnknown -----------------------------------------------------------
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // --- IMMNotificationClient ---------------------------------------------
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR id, DWORD state) override;
    STDMETHODIMP OnDeviceAdded(LPCWSTR id) override;
    STDMETHODIMP OnDeviceRemoved(LPCWSTR id) override;
    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR id) override;
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR id, const PROPERTYKEY key) override;

private:
    void ThreadProc();
    bool OpenStream();
    void CloseStream();
    void ConvertAndWrite(const BYTE* data, uint32_t frames);
    void RequestReinit();
    std::wstring WantedDeviceId() const;

    std::atomic<long> refCount_{ 1 };
    std::atomic<bool> running_{ false };
    std::atomic<bool> stop_{ false };
    std::atomic<bool> reinit_{ false };
    std::atomic<float> sampleRate_{ 48000.0f };

    mutable std::mutex mutex_;
    std::wstring wantedDeviceId_;
    std::wstring status_;
    RingBuffer* ring_ = nullptr;

    std::thread thread_;
    HANDLE wakeEvent_ = nullptr;
    HANDLE stopEvent_ = nullptr;

    // Owned by the capture thread only (COM MTA).
    ComPtr<IMMDeviceEnumerator> enumerator_;
    ComPtr<IMMDevice> device_;
    ComPtr<IAudioClient> audioClient_;
    ComPtr<IAudioCaptureClient> captureClient_;
    uint32_t channels_ = 2;
    uint32_t bits_ = 32;
    uint32_t blockAlign_ = 8;
    bool floatFormat_ = true;
    uint32_t bufferFrames_ = 0;
    uint32_t sleepMs_ = 5;
    std::vector<float> mono_;
};
