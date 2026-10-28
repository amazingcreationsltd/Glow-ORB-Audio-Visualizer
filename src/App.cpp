#include "App.h"
#include "Presets.h"
#include "Util.h"

namespace {

// Direct port of STATE_TABLE from script.js.
const App::ParamSet MakeIdle() {
    App::ParamSet p{};
    p[0] = 0.30f; p[1] = 0.52f; p[2] = 0.48f; p[3] = 0.90f;
    p[4] = 0.94f; p[5] = 0.68f; p[6] = 0.40f; p[7] = 0.16f;
    p[8] = 0.55f; p[9] = 0.00f; p[10] = 0.0f; p[11] = 1.0f;
    return p;
}
const App::ParamSet MakeListening() {
    App::ParamSet p{};
    p[0] = 0.55f; p[1] = 0.66f; p[2] = 0.70f; p[3] = 1.00f;
    p[4] = 1.00f; p[5] = 1.04f; p[6] = 0.00f; p[7] = 0.38f;
    p[8] = 0.85f; p[9] = 1.00f; p[10] = 0.0f; p[11] = 1.0f;
    return p;
}
const App::ParamSet MakeThinking() {
    App::ParamSet p{};
    p[0] = 1.00f; p[1] = 1.00f; p[2] = 1.00f; p[3] = 1.00f;
    p[4] = 1.00f; p[5] = 1.00f; p[6] = 0.00f; p[7] = 0.34f;
    p[8] = 0.80f; p[9] = 0.00f; p[10] = 0.0f; p[11] = 1.0f;
    return p;
}
const App::ParamSet MakeSpeaking() {
    App::ParamSet p{};
    p[0] = 0.78f; p[1] = 0.72f; p[2] = 0.90f; p[3] = 1.00f;
    p[4] = 1.00f; p[5] = 0.96f; p[6] = 0.00f; p[7] = 0.42f;
    p[8] = 0.80f; p[9] = 0.00f; p[10] = 1.0f; p[11] = 1.0f;
    return p;
}

} // namespace

App::~App() {
    running_ = false;
    capture_.Stop();
    SaveSettings();
    if (comInitialized_) CoUninitialize();
}

bool App::Init(HINSTANCE instance) {
    instance_ = instance;
    Glow::EnableDpiAwareness();

    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    comInitialized_ = SUCCEEDED(com);
    if (!comInitialized_) {
        Glow::Log(L"[GlowOrb] CoInitializeEx failed: %s", Glow::HrToString(com).c_str());
    }

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    settings_.Load(Glow::ConfigFilePath());
    ParseCommandLine();

    uint32_t presetCount = 0;
    Glow::GetPresets(presetCount);
    if (settings_.presetIndex < 0 || settings_.presetIndex >= static_cast<int>(presetCount)) {
        settings_.presetIndex = 0;
    }
    params_ = MakeIdle();
    targetParams_ = MakeIdle();

    if (!overlay_.Create(instance_, this)) {
        Glow::Fatal(L"GlowOrb", L"Could not create the overlay window. See the log for details.");
        return false;
    }

    const uint32_t size = static_cast<uint32_t>(settings_.orbSize);
    const bool wantTransparent = settings_.opaqueMode < 0.5f;
    if (!renderer_.Initialize(overlay_.Hwnd(), size, size, wantTransparent)) {
        Glow::Fatal(L"GlowOrb - DirectX 12",
                    L"Failed to initialize DirectX 12.\n\n"
                    L"Make sure you have a D3D12-capable GPU, an up-to-date graphics "
                    L"driver, and that shaders\\Orb.hlsl is next to the .exe.\n"
                    L"See %%APPDATA%%\\GlowOrb\\gloworb.log for details.");
        return false;
    }
    opaqueActive_ = renderer_.OpaqueFallback() || !wantTransparent;
    if (opaqueActive_) settings_.opaqueMode = 1.0f;
    overlay_.ApplyStyles();

    settingsWindow_.Create(instance_, this);

    capture_.SetDevice(settings_.deviceId);
    if (!capture_.Start(ringBuffer_)) {
        Glow::Log(L"[GlowOrb] Audio capture failed to start; visualizer will stay idle");
    }

    RegisterHotKey(overlay_.Hwnd(), 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'P');

    lastTime_ = Glow::NowSeconds();
    running_ = true;
    Glow::Log(L"[GlowOrb] Started. Click the tray icon for settings. Ctrl+Alt+P = next preset.");
    return true;
}

void App::ParseCommandLine() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&](float def) -> float {
            return (i + 1 < argc) ? static_cast<float>(wcstod(argv[++i], nullptr)) : def;
        };
        if (a == L"--opaque") settings_.opaqueMode = 1.0f;
        else if (a == L"--no-clickthrough") settings_.clickThrough = 0.0f;
        else if (a == L"--no-topmost") settings_.alwaysOnTop = 0.0f;
        else if (a == L"--size") settings_.orbSize = GlowClamp(next(560.0f), 200.0f, 1400.0f);
        else if (a == L"--gain") settings_.gain = GlowClamp(next(3.5f), 0.5f, 12.0f);
        else if (a == L"--preset") settings_.presetIndex = static_cast<int>(next(0.0f));
        else if (a == L"--monitor") settings_.monitorIndex = next(0.0f);
        else if (a == L"--position") settings_.position = next(0.0f);
    }
    LocalFree(argv);
}

int App::Run() {
    MSG msg = {};
    while (running_) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running_ = false;
                return static_cast<int>(msg.wParam);
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        const double now = Glow::NowSeconds();
        const double dt = std::min(0.05, std::max(0.0, now - lastTime_));
        lastTime_ = now;

        if (settings_.fpsLimit > 0.0f) {
            const double interval = 1.0 / static_cast<double>(settings_.fpsLimit);
            if (now - lastRender_ < interval) {
                Sleep(1);
                continue;
            }
        }
        lastRender_ = now;
        RenderFrame(dt);
    }
    return 0;
}

void App::ChooseState(double dt, const AudioAnalyzer::Frame& frame) {
    if (frame.level < 0.015f) quietTime_ += dt;
    else quietTime_ = 0.0;

    int state = 0; // idle
    if (settings_.stateMode >= 0.5f) {
        state = static_cast<int>(settings_.stateMode); // 1 idle, 2 speaking, 3 listening, 4 thinking
        if (state == 1) state = 0;
        else if (state == 2) state = 3;
        else if (state == 3) state = 1;
        else if (state == 4) state = 2;
    } else if (quietTime_ > 0.5) {
        state = 0;
    } else if (frame.level > 0.12f) {
        state = 3; // speaking
    } else {
        state = 1; // listening
    }

    switch (state) {
        case 1:  targetParams_ = MakeListening(); break;
        case 2:  targetParams_ = MakeThinking(); break;
        case 3:  targetParams_ = MakeSpeaking(); break;
        default: targetParams_ = MakeIdle(); break;
    }

    const float k = static_cast<float>(std::min(1.0, dt * 6.0));
    for (size_t i = 0; i < SP_COUNT; ++i) {
        params_[i] += (targetParams_[i] - params_[i]) * k;
    }
}

void App::RenderFrame(double dt) {
    if (settings_.paused > 0.5f) return;

    elapsed_ += dt;
    AudioAnalyzer::Frame frame;
    analyzer_.Update(dt, capture_.SampleRate(), settings_.gain, frame);
    ChooseState(dt, frame);

    const float level = frame.level;
    const float voice = params_[SP_VOICE] * level;
    sheetPhase_ += dt * params_[SP_SPEED] * (1.0 + 0.7 * voice);
    haloAngle_ = fmod(haloAngle_ + dt * (36.0 + level * 220.0) / 360.0, 1.0);
    const float orbScale = 1.0f + level * settings_.sizePulse;

    uint32_t presetCount = 0;
    const Glow::Preset* presets = Glow::GetPresets(presetCount);
    const Glow::Preset& preset = presets[static_cast<size_t>(settings_.presetIndex)];
    const Glow::Palette pal = Glow::BuildPalette(preset.from, preset.to,
                                                 params_[SP_MUTE], preset.halo);

    const float w = static_cast<float>(settings_.orbSize);
    const float h = static_cast<float>(settings_.orbSize);
    const float virtualSize = std::min(w, h) * 0.58f; // leaves room for the halo

    OrbConstants cb = {};
    cb.uResolution = { w, h };
    cb.uSize = virtualSize;
    cb.uLevel = level;
    cb.uParams0 = { params_[SP_WARP] * 3.2f + 0.85f * voice,
                    params_[SP_RIDGE] + 0.35f * voice + 0.25f * params_[SP_HEAR] * level,
                    params_[SP_SHARP] * 2.2f,
                    params_[SP_ZOOM] };
    cb.uParams1 = { params_[SP_EXPOSURE] * 1.9f *
                        (1.0f + 0.12f * voice + 0.1f * params_[SP_HEAR] * level),
                    params_[SP_GLOW] * 0.55f * settings_.glowIntensity,
                    params_[SP_RIM],
                    params_[SP_HEAR] };
    cb.uParams2 = { params_[SP_FADE] * settings_.opacity,
                    frame.bass, frame.mid, frame.treble };
    cb.uParams3 = { frame.beat, static_cast<float>(haloAngle_),
                    settings_.haloNoise, settings_.particleGlow };
    cb.uParams4 = { static_cast<float>(sheetPhase_), voice,
                    opaqueActive_ ? 1.0f : 0.0f, static_cast<float>(elapsed_) };
    cb.uParams5 = { orbScale, 0.0f, 0.0f, 0.0f };

    cb.uColA = pal.a; cb.uColB = pal.b; cb.uColC = pal.c; cb.uColD = pal.d;
    cb.uHi = pal.hi; cb.uCool = pal.cool; cb.uWarm = pal.warm;
    cb.uGlowCol = pal.glow;
    for (uint32_t i = 0; i < 5; ++i) cb.uHalo[i] = pal.halo[i];

    uint8_t wave[WAVE_SIZE];
    for (uint32_t i = 0; i < WAVE_SIZE; ++i) {
        wave[i] = static_cast<uint8_t>(GlowClamp(128.0f + frame.waveform[i] * 127.0f,
                                                 0.0f, 255.0f));
    }

    particles_.Update(dt, frame, settings_, static_cast<uint32_t>(w),
                      static_cast<uint32_t>(h), orbScale, virtualSize);

    const bool vsync = settings_.fpsLimit <= 0.0f;
    renderer_.Render(cb, wave, particles_.Data(), particles_.Count(), vsync);

    if (elapsed_ - lastTrayUpdate_ > 1.0) {
        lastTrayUpdate_ = elapsed_;
        overlay_.UpdateTrayTip(StatusText());
    }
}

std::wstring App::StatusText() const {
    std::wstring status = capture_.Status();
    if (status.empty()) status = L"no audio device";
    wchar_t buf[64];
    swprintf(buf, 64, L" | DSP %s", capture_.Running() ? L"on" : L"off");
    return status + buf;
}

std::vector<std::pair<std::wstring, std::wstring>> App::GetAudioDevices() {
    return AudioCapture::EnumDevices();
}

void App::OnSettingsChanged() {
    settings_.orbSize = GlowClamp(settings_.orbSize, 200.0f, 1400.0f);
    settings_.gain = GlowClamp(settings_.gain, 0.5f, 12.0f);
    settings_.opacity = GlowClamp(settings_.opacity, 0.1f, 1.0f);
    settings_.particleDensity = GlowClamp(settings_.particleDensity, 0.0f,
                                          static_cast<float>(MAX_PARTICLES));

    const uint32_t size = static_cast<uint32_t>(settings_.orbSize);
    overlay_.ApplyStyles();
    overlay_.Resize(size);
    renderer_.Resize(size, size);
    SaveSettings();
}

void App::OnOpaqueModeChanged() {
    const bool wantTransparent = settings_.opaqueMode < 0.5f;
    const uint32_t size = static_cast<uint32_t>(settings_.orbSize);

    renderer_.Shutdown();
    opaqueActive_ = !wantTransparent;
    overlay_.ApplyStyles();  // fix layered/NOREDIRECTIONBITMAP before new swapchain

    if (!renderer_.Initialize(overlay_.Hwnd(), size, size, wantTransparent)) {
        Glow::Log(L"[GlowOrb] Renderer re-init failed after mode switch");
        opaqueActive_ = true;
        settings_.opaqueMode = 1.0f;
        renderer_.Initialize(overlay_.Hwnd(), size, size, false);
    }
    opaqueActive_ = renderer_.OpaqueFallback() || !wantTransparent;
    if (opaqueActive_) settings_.opaqueMode = 1.0f;
    overlay_.ApplyStyles();
    SaveSettings();
    settingsWindow_.Refresh();
}

void App::OnDisplayChanged() {
    overlay_.Reposition();
}

void App::SetPreset(int index) {
    uint32_t count = 0;
    Glow::GetPresets(count);
    const int n = static_cast<int>(count);
    index = ((index % n) + n) % n;
    settings_.presetIndex = index;
    SaveSettings();
    if (settingsWindow_.Visible()) settingsWindow_.Refresh();
}

void App::SetAudioDevice(const std::wstring& id) {
    settings_.deviceId = id;
    capture_.SetDevice(id);
    ringBuffer_.Clear();
    analyzer_.Reset();
    SaveSettings();
    if (settingsWindow_.Visible()) settingsWindow_.Refresh();
}

void App::ToggleSettingsWindow() {
    if (settingsWindow_.Visible()) settingsWindow_.Hide();
    else settingsWindow_.Show();
}

void App::ResetSettings() {
    settings_.Reset();
    params_ = MakeIdle();
    targetParams_ = MakeIdle();
    particles_.Clear();
    capture_.SetDevice(L"");
    ringBuffer_.Clear();
    analyzer_.Reset();
    OnSettingsChanged();
    settingsWindow_.Refresh();
}

void App::SaveSettings() {
    settings_.Save(Glow::ConfigFilePath());
}

void App::OnExit() {
    running_ = false;
    SaveSettings();
    UnregisterHotKey(overlay_.Hwnd(), 1);
    PostQuitMessage(0);
}
