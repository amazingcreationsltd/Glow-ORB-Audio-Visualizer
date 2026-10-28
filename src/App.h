#pragma once
#include "Common.h"
#include "Settings.h"
#include "RingBuffer.h"
#include "AudioCapture.h"
#include "AudioAnalyzer.h"
#include "ParticleSystem.h"
#include "D3D12Renderer.h"
#include "OverlayWindow.h"
#include "SettingsWindow.h"
#include "Palette.h"

class App {
public:
    enum ParamIndex : size_t {
        SP_SPEED, SP_WARP, SP_RIDGE, SP_SHARP, SP_ZOOM, SP_EXPOSURE,
        SP_MUTE, SP_GLOW, SP_RIM, SP_HEAR, SP_VOICE, SP_FADE, SP_COUNT
    };
    using ParamSet = std::array<float, SP_COUNT>;

    App() = default;
    ~App();

    bool Init(HINSTANCE instance);
    int Run();

    Settings& GetSettings() { return settings_; }
    const Settings& GetSettings() const { return settings_; }

    void OnSettingsChanged();
    void OnOpaqueModeChanged();
    void OnDisplayChanged();
    void OnExit();
    void SetPreset(int index);
    void SetAudioDevice(const std::wstring& id);
    void ToggleSettingsWindow();
    void ResetSettings();
    void SaveSettings();

    std::vector<std::pair<std::wstring, std::wstring>> GetAudioDevices();
    std::wstring StatusText() const;
    bool RendererIsOpaqueFallback() const { return opaqueActive_; }

private:
    void ParseCommandLine();
    void RenderFrame(double dt);
    void ChooseState(double dt, const AudioAnalyzer::Frame& frame);

    HINSTANCE instance_ = nullptr;
    bool comInitialized_ = false;

    Settings settings_;
    RingBuffer ringBuffer_;
    AudioCapture capture_;
    AudioAnalyzer analyzer_{ ringBuffer_ };
    ParticleSystem particles_;
    D3D12Renderer renderer_;
    OverlayWindow overlay_;
    SettingsWindow settingsWindow_;

    ParamSet params_{};
    ParamSet targetParams_{};
    double sheetPhase_ = 1.7;
    double haloAngle_ = 0.0;
    double elapsed_ = 0.0;
    double quietTime_ = 0.0;
    double lastTime_ = 0.0;
    double lastRender_ = 0.0;
    double lastTrayUpdate_ = 0.0;
    bool opaqueActive_ = false;
    bool running_ = false;
};
