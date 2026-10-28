#include "SettingsWindow.h"
#include "App.h"
#include "Presets.h"
#include "Util.h"

namespace {
const wchar_t* kSettingsClass = L"GlowOrbSettingsWindow";

const wchar_t* kPositionNames[] = {
    L"Center", L"Bottom center", L"Top center", L"Top left",
    L"Top right", L"Bottom left", L"Bottom right", L"Custom (drag to move)"
};
const wchar_t* kStateNames[] = {
    L"Auto (react to audio)", L"Idle", L"Speaking", L"Listening", L"Thinking"
};
const wchar_t* kCheckNames[] = {
    L"Stardust particles", L"Click-through", L"Always on top",
    L"Hide from screen capture", L"Opaque window (compatibility)", L"Pause visualizer"
};
const wchar_t* kSliderNames[] = {
    L"Input gain", L"Orb size (px)", L"Size pulse", L"Glow intensity",
    L"Halo noise", L"Opacity", L"Particle density", L"Particle speed",
    L"Particle spread", L"Particle glow"
};
} // namespace

bool SettingsWindow::Create(HINSTANCE instance, App* app) {
    instance_ = instance;
    app_ = app;
    font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SettingsWindow::WndProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kSettingsClass;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kSettingsClass,
                            L"GlowOrb - Settings", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                            CW_USEDEFAULT, CW_USEDEFAULT, 716, 622,
                            nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    const int labelX = 14, comboX = 150, comboW = 520;
    struct ComboRow { const wchar_t* label; HWND* target; int id; };
    ComboRow rows[] = {
        { L"Color preset", &presetCombo_, Glow::IDS_PRESET_COMBO },
        { L"Audio device", &deviceCombo_, Glow::IDS_DEVICE_COMBO },
        { L"Position", &positionCombo_, Glow::IDS_POSITION_COMBO },
        { L"Visual state", &stateCombo_, Glow::IDS_STATE_COMBO },
        { L"Monitor", &monitorCombo_, Glow::IDS_MONITOR_COMBO },
    };
    int y = 14;
    for (const auto& row : rows) {
        MakeLabel(row.label, labelX, y + 3, 130, 20, 0);
        *row.target = MakeCombo(comboX, y, comboW, 200, row.id);
        y += 30;
    }

    Settings& s = app_->GetSettings();
    float* targets[] = {
        &s.gain, &s.orbSize, &s.sizePulse, &s.glowIntensity, &s.haloNoise,
        &s.opacity, &s.particleDensity, &s.particleSpeed, &s.particleSpread,
        &s.particleGlow
    };
    const float mins[] = { 0.5f, 200.0f, 0.0f, 0.0f, 0.0f,
                           0.1f, 0.0f, 0.1f, 5.0f, 0.2f };
    const float maxs[] = { 12.0f, 1400.0f, 0.8f, 2.0f, 1.0f,
                           1.0f, 1400.0f, 5.0f, 200.0f, 2.0f };
    y = 176;
    for (int i = 0; i < 10; ++i) {
        AddSlider(kSliderNames[i], targets[i], mins[i], maxs[i],
                  i == 1 || i == 6, y);
        y += 26;
    }

    float* checkTargets[] = {
        &s.particlesEnabled, &s.clickThrough, &s.alwaysOnTop,
        &s.excludeFromCapture, &s.opaqueMode, &s.paused
    };
    (void)checkTargets; // targets are read back by index in OnCommand
    int cy = 448;
    for (int i = 0; i < 6; ++i) {
        const int col = i % 2;
        const int row = i / 2;
        checks_.push_back(MakeCheck(kCheckNames[i], 14 + col * 340,
                                     cy + row * 24, 330, 22,
                                    Glow::IDS_CHECK_BASE + i));
    }

    MakeButton(L"Reset defaults", 14, 534, 130, 26, Glow::IDS_BUTTON_RESET);
    MakeButton(L"Open config folder", 154, 534, 160, 26, Glow::IDS_BUTTON_FOLDER);
    MakeButton(L"Close", 560, 534, 110, 26, Glow::IDS_BUTTON_CLOSE);

    FillCombos();
    Refresh();
    return true;
}

HWND SettingsWindow::MakeLabel(const wchar_t* text, int x, int y, int w, int h, int) {
    HWND label = CreateWindowW(WC_STATICW, text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                               x, y, w, h, hwnd_, nullptr, instance_, nullptr);
    SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    return label;
}

HWND SettingsWindow::MakeCombo(int x, int y, int w, int h, int id) {
    HWND c = CreateWindowW(WC_COMBOBOXW, L"",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
                           CBS_HASSTRINGS | WS_VSCROLL,
                           x, y, w, h, hwnd_, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
                           instance_, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    return c;
}

HWND SettingsWindow::MakeCheck(const wchar_t* text, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowW(WC_BUTTONW, text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                           x, y, w, h, hwnd_, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
                           instance_, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    return c;
}

HWND SettingsWindow::MakeButton(const wchar_t* text, int x, int y, int w, int h, int id) {
    HWND b = CreateWindowW(WC_BUTTONW, text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                           x, y, w, h, hwnd_, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
                           instance_, nullptr);
    SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    return b;
}

void SettingsWindow::AddSlider(const wchar_t* label, float* target, float min,
                               float max, bool integer, int y) {
    Slider s{};
    s.target = target;
    s.min = min;
    s.max = max;
    s.integer = integer;
    s.id = Glow::IDS_SLIDER_BASE + static_cast<int>(sliders_.size());

    MakeLabel(label, 14, y + 2, 130, 20, 0);
    s.bar = CreateWindowW(TRACKBAR_CLASSW, L"",
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
                          150, y, 410, 22, hwnd_,
                          reinterpret_cast<HMENU>(static_cast<intptr_t>(s.id)),
                          instance_, nullptr);
    SendMessageW(s.bar, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
    SendMessageW(s.bar, TBM_SETPAGESIZE, 0, 10);
    s.value = MakeLabel(L"", 570, y + 2, 110, 20, 0);
    sliders_.push_back(s);
    UpdateSliderLabel(sliders_.back());
}

void SettingsWindow::UpdateSliderLabel(const Slider& s) {
    const int pos = static_cast<int>(SendMessageW(s.bar, TBM_GETPOS, 0, 0));
    const float value = s.min + (static_cast<float>(pos) / 1000.0f) * (s.max - s.min);
    wchar_t buf[64];
    if (s.integer) {
        swprintf(buf, 64, L"%d", static_cast<int>(value));
    } else {
        swprintf(buf, 64, L"%.2f", value);
    }
    SetWindowTextW(s.value, buf);
}

void SettingsWindow::FillCombos() {
    using namespace Glow;
    uint32_t presetCount = 0;
    const Preset* presets = GetPresets(presetCount);
    for (uint32_t i = 0; i < presetCount; ++i) {
        SendMessageW(presetCombo_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(presets[i].name));
    }
    for (const wchar_t* name : kPositionNames) {
        SendMessageW(positionCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    }
    for (const wchar_t* name : kStateNames) {
        SendMessageW(stateCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    }
    auto devices = app_->GetAudioDevices();
    deviceIds_.clear();
    deviceIds_.push_back(L"");
    SendMessageW(deviceCombo_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Default (follow system)"));
    for (const auto& d : devices) {
        deviceIds_.push_back(d.first);
        SendMessageW(deviceCombo_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(d.second.c_str()));
    }
    auto monitors = Glow::GetMonitors();
    for (size_t i = 0; i < monitors.size(); ++i) {
        std::wstring name = (i == 0) ? L"Display 1 (primary)"
                                     : (L"Display " + std::to_wstring(i + 1));
        SendMessageW(monitorCombo_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(name.c_str()));
    }
}

void SettingsWindow::Refresh() {
    if (!hwnd_) return;
    const Settings& s = app_->GetSettings();

    // Rebuild device combo if the endpoint list changed.
    if (SendMessageW(deviceCombo_, CB_GETCOUNT, 0, 0) !=
        static_cast<LRESULT>(deviceIds_.size())) {
        while (SendMessageW(deviceCombo_, CB_GETCOUNT, 0, 0) > 0) {
            SendMessageW(deviceCombo_, CB_DELETESTRING, 0, 0);
        }
        deviceIds_.clear();
        deviceIds_.push_back(L"");
        SendMessageW(deviceCombo_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Default (follow system)"));
        for (const auto& d : app_->GetAudioDevices()) {
            deviceIds_.push_back(d.first);
            SendMessageW(deviceCombo_, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(d.second.c_str()));
        }
    }

    SendMessageW(presetCombo_, CB_SETCURSEL, static_cast<WPARAM>(s.presetIndex), 0);
    SendMessageW(positionCombo_, CB_SETCURSEL, static_cast<WPARAM>(static_cast<int>(s.position)), 0);
    SendMessageW(stateCombo_, CB_SETCURSEL, static_cast<WPARAM>(static_cast<int>(s.stateMode)), 0);
    SendMessageW(monitorCombo_, CB_SETCURSEL, static_cast<WPARAM>(static_cast<int>(s.monitorIndex)), 0);

    int devIndex = 0;
    for (size_t i = 0; i < deviceIds_.size(); ++i) {
        if (deviceIds_[i] == s.deviceId) { devIndex = static_cast<int>(i); break; }
    }
    SendMessageW(deviceCombo_, CB_SETCURSEL, static_cast<WPARAM>(devIndex), 0);

    const float* checkValues[] = {
        &s.particlesEnabled, &s.clickThrough, &s.alwaysOnTop,
        &s.excludeFromCapture, &s.opaqueMode, &s.paused
    };
    for (size_t i = 0; i < checks_.size(); ++i) {
        SendMessageW(checks_[i], BM_SETCHECK,
                     (*checkValues[i] > 0.5f) ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    for (auto& sl : sliders_) {
        const float t = (*sl.target - sl.min) / (sl.max - sl.min);
        SendMessageW(sl.bar, TBM_SETPOS, TRUE,
                     static_cast<LPARAM>(static_cast<int>(GlowClamp(t, 0.0f, 1.0f) * 1000.0f)));
        UpdateSliderLabel(sl);
    }
}

void SettingsWindow::Show() {
    if (!hwnd_) return;
    Refresh();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void SettingsWindow::Hide() {
    if (!hwnd_) return;
    ShowWindow(hwnd_, SW_HIDE);
    app_->SaveSettings();
}

void SettingsWindow::Destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void SettingsWindow::OnScroll(HWND bar) {
    for (auto& sl : sliders_) {
        if (sl.bar != bar) continue;
        const int pos = static_cast<int>(SendMessageW(bar, TBM_GETPOS, 0, 0));
        *sl.target = sl.min + (static_cast<float>(pos) / 1000.0f) * (sl.max - sl.min);
        if (sl.integer) *sl.target = static_cast<float>(static_cast<int>(*sl.target));
        UpdateSliderLabel(sl);
        app_->OnSettingsChanged();
        return;
    }
}

void SettingsWindow::OnCommand(WPARAM wParam, LPARAM lParam) {
    using namespace Glow;
    const UINT id = LOWORD(wParam);
    const UINT code = HIWORD(wParam);
    Settings& s = app_->GetSettings();

    if (code == CBN_SELCHANGE) {
        const int sel = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lParam),
                                                     CB_GETCURSEL, 0, 0));
        if (sel < 0) return;
        switch (id) {
            case IDS_PRESET_COMBO:  app_->SetPreset(sel); break;
            case IDS_POSITION_COMBO: s.position = static_cast<float>(sel); app_->OnSettingsChanged(); break;
            case IDS_STATE_COMBO:   s.stateMode = static_cast<float>(sel); app_->OnSettingsChanged(); break;
            case IDS_MONITOR_COMBO: s.monitorIndex = static_cast<float>(sel); app_->OnSettingsChanged(); break;
            case IDS_DEVICE_COMBO:
                if (sel >= 0 && sel < static_cast<int>(deviceIds_.size())) {
                    app_->SetAudioDevice(deviceIds_[static_cast<size_t>(sel)]);
                }
                break;
            default: break;
        }
        return;
    }

    if (id >= IDS_CHECK_BASE && id < IDS_CHECK_BASE + 8) {
        const int index = static_cast<int>(id - IDS_CHECK_BASE);
        float* targets[] = {
            &s.particlesEnabled, &s.clickThrough, &s.alwaysOnTop,
            &s.excludeFromCapture, &s.opaqueMode, &s.paused
        };
        if (index < 6) {
            *targets[index] = (*targets[index] > 0.5f) ? 0.0f : 1.0f;
            if (id == IDS_CHECK_BASE + 4) app_->OnOpaqueModeChanged();
            else app_->OnSettingsChanged();
        }
        return;
    }

    switch (id) {
        case IDS_BUTTON_RESET:
            app_->ResetSettings();
            Refresh();
            break;
        case IDS_BUTTON_CLOSE:
            Hide();
            break;
        case IDS_BUTTON_FOLDER:
            ShellExecuteW(nullptr, L"open", Glow::ConfigDir().c_str(), nullptr,
                          nullptr, SW_SHOWNORMAL);
            break;
        default: break;
    }
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsWindow* self = reinterpret_cast<SettingsWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_COMMAND:
            self->OnCommand(wParam, lParam);
            return 0;
        case WM_HSCROLL:
        case WM_VSCROLL:
            self->OnScroll(reinterpret_cast<HWND>(lParam));
            return 0;
        case WM_CLOSE:
            self->Hide();
            return 0;
        case WM_DESTROY:
            self->app_->SaveSettings();
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
