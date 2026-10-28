#include "OverlayWindow.h"
#include "App.h"
#include "Presets.h"
#include "Util.h"

namespace {

const wchar_t* kWindowClass = L"GlowOrbOverlayWindow";
const wchar_t* kWindowTitle = L"GlowOrb";

const wchar_t* kPositionNames[] = {
    L"Center", L"Bottom center", L"Top center", L"Top left",
    L"Top right", L"Bottom left", L"Bottom right", L"Custom (drag to move)"
};
const wchar_t* kStateNames[] = {
    L"Auto (react to audio)", L"Idle", L"Speaking", L"Listening", L"Thinking"
};
const float kGains[] = { 1.0f, 2.0f, 3.5f, 5.0f, 7.0f, 10.0f };
const int kSizeSteps[] = { -100, -50, -25, 25, 50, 100 };

void AppendChecked(HMENU menu, UINT id, const wchar_t* text, bool checked,
                   bool enabled = true) {
    UINT flags = MF_STRING | (checked ? MF_CHECKED : MF_UNCHECKED);
    if (!enabled) flags |= MF_GRAYED | MF_DISABLED;
    AppendMenuW(menu, flags, id, text);
}

} // namespace

bool OverlayWindow::Create(HINSTANCE instance, App* app) {
    instance_ = instance;
    app_ = app;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &OverlayWindow::WndProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClass;

    // Icon is loaded from disk (no .rc needed) with a stock fallback.
    const std::wstring iconPath = Glow::ExeDir() + L"\\assets\\gloworb.ico";
    icon_ = reinterpret_cast<HICON>(LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON,
                                               32, 32, LR_LOADFROMFILE));
    iconOwned_ = (icon_ != nullptr);
    if (!icon_) icon_ = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIcon = icon_;
    wc.hIconSm = icon_;
    RegisterClassExW(&wc);

    const uint32_t size = static_cast<uint32_t>(app->GetSettings().orbSize);
    LONG exStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED |
                   WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT | WS_EX_TOPMOST;

    hwnd_ = CreateWindowExW(exStyle, kWindowClass, kWindowTitle,
                            WS_POPUP | WS_VISIBLE,
                            0, 0, static_cast<int>(size), static_cast<int>(size),
                            nullptr, nullptr, instance_, this);
    if (!hwnd_) {
        Glow::Log(L"[GlowOrb] CreateWindowExW failed: %lu", GetLastError());
        return false;
    }

    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    ApplyStyles();
    Reposition();
    AddTrayIcon();
    return true;
}

void OverlayWindow::Destroy() {
    RemoveTrayIcon();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (icon_ && iconOwned_) DestroyIcon(icon_);
    icon_ = nullptr;
}

void OverlayWindow::AddTrayIcon() {
    tray_ = {};
    tray_.cbSize = sizeof(tray_);
    tray_.hWnd = hwnd_;
    tray_.uID = 1;
    tray_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray_.uCallbackMessage = Glow::WM_TRAY_CALLBACK;
    tray_.hIcon = icon_;
    wcsncpy(tray_.szTip, L"GlowOrb - system audio visualizer", 127);
    tray_.szTip[127] = 0;
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &tray_);
    if (!trayAdded_) Glow::Log(L"[GlowOrb] Shell_NotifyIcon(NIM_ADD) failed");
}

void OverlayWindow::RemoveTrayIcon() {
    if (trayAdded_) {
        Shell_NotifyIconW(NIM_DELETE, &tray_);
        trayAdded_ = false;
    }
}

void OverlayWindow::UpdateTrayTip(const std::wstring& text) {
    if (!trayAdded_) return;
    tray_.uFlags = NIF_TIP | NIF_MESSAGE;
    std::wstring tip = L"GlowOrb\n" + text;
    if (tip.size() > 127) tip.resize(127);
    wcsncpy(tray_.szTip, tip.c_str(), 127);
    tray_.szTip[127] = 0;
    Shell_NotifyIconW(NIM_MODIFY, &tray_);
}

void OverlayWindow::ApplyStyles() {
    if (!hwnd_) return;
    Settings& s = app_->GetSettings();
    const bool opaque = (s.opaqueMode > 0.5f) || app_->RendererIsOpaqueFallback();

    LONG ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    const LONG preserve = ~(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                            WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW |
                            WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW);
    ex &= preserve;
    ex |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (!opaque) {
        ex |= WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP;
        if (s.clickThrough > 0.5f) ex |= WS_EX_TRANSPARENT;
    }
    if (s.alwaysOnTop > 0.5f) ex |= WS_EX_TOPMOST;
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex);

    if (ex & WS_EX_LAYERED) {
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    }
    SetWindowDisplayAffinity(hwnd_, s.excludeFromCapture > 0.5f
                                       ? WDA_EXCLUDEFROMCAPTURE
                                       : WDA_NONE);
    SetWindowPos(hwnd_, s.alwaysOnTop > 0.5f ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED |
                 SWP_SHOWWINDOW);
}

void OverlayWindow::Reposition() {
    if (!hwnd_) return;
    Settings& s = app_->GetSettings();
    auto monitors = Glow::GetMonitors();
    const int idx = static_cast<int>(GlowClamp(s.monitorIndex, 0.0f,
                                               static_cast<float>(monitors.size() - 1)));
    const Glow::MonitorRect& m = monitors[static_cast<size_t>(idx)];
    const int size = static_cast<int>(s.orbSize);
    const int pad = 28;

    int x = m.left + (m.Width() - size) / 2;
    int y = m.top + (m.Height() - size) / 2;
    switch (static_cast<OrbPosition>(static_cast<int>(s.position))) {
        case OrbPosition::Center: break;
        case OrbPosition::BottomCenter: y = m.bottom - size - pad; break;
        case OrbPosition::TopCenter:   y = m.top + pad; break;
        case OrbPosition::TopLeft:     x = m.left + pad; y = m.top + pad; break;
        case OrbPosition::TopRight:    x = m.right - size - pad; y = m.top + pad; break;
        case OrbPosition::BottomLeft:  x = m.left + pad; y = m.bottom - size - pad; break;
        case OrbPosition::BottomRight: x = m.right - size - pad; y = m.bottom - size - pad; break;
        case OrbPosition::Custom:
            x = static_cast<int>(s.customX);
            y = static_cast<int>(s.customY);
            break;
        default: break;
    }
    SetWindowPos(hwnd_, nullptr, x, y, size, size,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
}

void OverlayWindow::Resize(uint32_t size) {
    if (!hwnd_) return;
    SetWindowPos(hwnd_, nullptr, 0, 0, static_cast<int>(size), static_cast<int>(size),
                 SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    Reposition();
}

void OverlayWindow::ShowContextMenu() {
    HMENU menu = BuildMenu();
    if (!menu) return;
    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
                   pt.x, pt.y, 0, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void OverlayWindow::RefreshTrayMenu() { /* menu is rebuilt on every right-click */ }

void OverlayWindow::ApplyMenuChecks(HMENU menu) {
    const Settings& s = app_->GetSettings();
    CheckMenuItem(menu, Glow::ID_TOGGLE_PARTICLES,
                  MF_BYCOMMAND | (s.particlesEnabled > 0.5f ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, Glow::ID_TOGGLE_CLICKTHROUGH,
                  MF_BYCOMMAND | (s.clickThrough > 0.5f ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, Glow::ID_TOGGLE_TOPMOST,
                  MF_BYCOMMAND | (s.alwaysOnTop > 0.5f ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, Glow::ID_TOGGLE_PAUSE,
                  MF_BYCOMMAND | (s.paused > 0.5f ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, Glow::ID_TOGGLE_CAPTURE,
                  MF_BYCOMMAND | (s.excludeFromCapture > 0.5f ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, Glow::ID_TOGGLE_OPAQUE,
                  MF_BYCOMMAND | (s.opaqueMode > 0.5f ? MF_CHECKED : MF_UNCHECKED));

    uint32_t presetCount = 0;
    Glow::GetPresets(presetCount);
    HMENU presetSub = GetSubMenu(menu, 2);
    if (presetSub) {
        for (uint32_t i = 0; i < presetCount; ++i) {
            CheckMenuItem(presetSub, static_cast<UINT>(i),
                          MF_BYPOSITION | (static_cast<int>(i) == s.presetIndex
                                               ? MF_CHECKED : MF_UNCHECKED));
        }
    }
}

HMENU OverlayWindow::BuildMenu() {
    Settings& s = app_->GetSettings();
    HMENU menu = CreatePopupMenu();
    if (!menu) return nullptr;
    using namespace Glow;

    std::wstring header = L"GlowOrb  |  " + app_->StatusText();
    AppendMenuW(menu, MF_STRING | MF_GRAYED | MF_DISABLED, 0, header.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // 2: presets
    uint32_t presetCount = 0;
    const Preset* presets = GetPresets(presetCount);
    HMENU presetMenu = CreatePopupMenu();
    for (uint32_t i = 0; i < presetCount; ++i) {
        AppendMenuW(presetMenu, MF_STRING, ID_PRESET_BASE + i, presets[i].name);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(presetMenu), L"Color preset");

    // 3: audio device
    HMENU deviceMenu = CreatePopupMenu();
    auto devices = app_->GetAudioDevices();
    AppendMenuW(deviceMenu, MF_STRING, ID_DEVICE_BASE, L"Default (follow system)");
    for (size_t i = 0; i < devices.size() && i < 32; ++i) {
        std::wstring label = devices[i].second;
        if (label.size() > 48) label = label.substr(0, 45) + L"...";
        AppendMenuW(deviceMenu, MF_STRING, ID_DEVICE_BASE + static_cast<UINT>(i) + 1,
                    label.c_str());
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(deviceMenu), L"Audio device");

    // 4: position
    HMENU positionMenu = CreatePopupMenu();
    for (int i = 0; i < static_cast<int>(OrbPosition::Count); ++i) {
        AppendMenuW(positionMenu, MF_STRING, ID_POSITION_BASE + i, kPositionNames[i]);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(positionMenu), L"Position");

    // 5: monitor
    HMENU monitorMenu = CreatePopupMenu();
    auto monitors = Glow::GetMonitors();
    for (size_t i = 0; i < monitors.size(); ++i) {
        std::wstring label = (i == 0 ? L"Display 1 (primary)" :
                              L"Display " + std::to_wstring(i + 1));
        AppendMenuW(monitorMenu, MF_STRING, ID_MONITOR_BASE + static_cast<UINT>(i),
                    label.c_str());
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(monitorMenu), L"Monitor");

    // 6: state
    HMENU stateMenu = CreatePopupMenu();
    for (int i = 0; i < 5; ++i) {
        AppendMenuW(stateMenu, MF_STRING, ID_STATE_BASE + i, kStateNames[i]);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(stateMenu), L"Visual state");

    // 7: size
    HMENU sizeMenu = CreatePopupMenu();
    AppendMenuW(sizeMenu, MF_STRING, ID_SIZE_BASE, L"Reset to 560 px");
    for (size_t i = 0; i < sizeof(kSizeSteps) / sizeof(kSizeSteps[0]); ++i) {
        int d = kSizeSteps[i];
        std::wstring label = (d > 0 ? L"+" : L"") + std::to_wstring(d) + L" px";
        AppendMenuW(sizeMenu, MF_STRING, ID_SIZE_BASE + static_cast<UINT>(i) + 1,
                    label.c_str());
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sizeMenu), L"Orb size");

    // 8: gain
    HMENU gainMenu = CreatePopupMenu();
    for (size_t i = 0; i < sizeof(kGains) / sizeof(kGains[0]); ++i) {
        wchar_t buf[32];
        swprintf(buf, 32, L"%.1fx", kGains[i]);
        AppendMenuW(gainMenu, MF_STRING, ID_GAIN_BASE + static_cast<UINT>(i), buf);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(gainMenu), L"Input gain");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendChecked(menu, ID_NEXT_PRESET, L"Next preset  (Ctrl+Alt+P)", false);
    AppendChecked(menu, ID_TOGGLE_PARTICLES, L"Stardust particles", s.particlesEnabled > 0.5f);
    AppendChecked(menu, ID_TOGGLE_CLICKTHROUGH, L"Click-through", s.clickThrough > 0.5f);
    AppendChecked(menu, ID_TOGGLE_TOPMOST, L"Always on top", s.alwaysOnTop > 0.5f);
    AppendChecked(menu, ID_TOGGLE_CAPTURE, L"Hide from screen capture", s.excludeFromCapture > 0.5f);
    AppendChecked(menu, ID_TOGGLE_OPAQUE, L"Opaque window mode (compatibility)",
                  s.opaqueMode > 0.5f);
    AppendChecked(menu, ID_TOGGLE_PAUSE, L"Pause visualizer", s.paused > 0.5f);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_SETTINGS, L"Settings...");
    AppendMenuW(menu, MF_STRING, ID_OPEN_CONFIG, L"Open config folder");
    AppendMenuW(menu, MF_STRING, ID_ABOUT, L"About GlowOrb");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");

    ApplyMenuChecks(menu);
    return menu;
}

void OverlayWindow::OnCommand(UINT id) {
    using namespace Glow;
    Settings& s = app_->GetSettings();
    uint32_t presetCount = 0;
    GetPresets(presetCount);

    if (id >= ID_PRESET_BASE && id < ID_PRESET_BASE + presetCount) {
        app_->SetPreset(static_cast<int>(id - ID_PRESET_BASE));
        return;
    }
    if (id >= ID_DEVICE_BASE && id <= ID_DEVICE_BASE + 64) {
        auto devices = app_->GetAudioDevices();
        const UINT index = id - ID_DEVICE_BASE;
        std::wstring deviceId;
        if (index > 0 && index - 1 < devices.size()) deviceId = devices[index - 1].first;
        app_->SetAudioDevice(deviceId);
        return;
    }
    if (id >= ID_POSITION_BASE && id < ID_POSITION_BASE + static_cast<UINT>(OrbPosition::Count)) {
        s.position = static_cast<float>(id - ID_POSITION_BASE);
        app_->OnSettingsChanged();
        return;
    }
    if (id >= ID_MONITOR_BASE && id < ID_MONITOR_BASE + 16) {
        s.monitorIndex = static_cast<float>(id - ID_MONITOR_BASE);
        app_->OnSettingsChanged();
        return;
    }
    if (id >= ID_STATE_BASE && id < ID_STATE_BASE + 5) {
        s.stateMode = static_cast<float>(id - ID_STATE_BASE);
        app_->OnSettingsChanged();
        return;
    }
    if (id >= ID_GAIN_BASE && id < ID_GAIN_BASE + sizeof(kGains) / sizeof(kGains[0])) {
        s.gain = kGains[id - ID_GAIN_BASE];
        app_->OnSettingsChanged();
        return;
    }
    if (id >= ID_SIZE_BASE && id <= ID_SIZE_BASE + sizeof(kSizeSteps) / sizeof(kSizeSteps[0])) {
        const UINT index = id - ID_SIZE_BASE;
        if (index == 0) {
            s.orbSize = 560.0f;
        } else {
            s.orbSize = GlowClamp(s.orbSize + static_cast<float>(kSizeSteps[index - 1]),
                                  200.0f, 1400.0f);
        }
        app_->OnSettingsChanged();
        return;
    }

    switch (id) {
        case ID_TOGGLE_PARTICLES:
            s.particlesEnabled = s.particlesEnabled > 0.5f ? 0.0f : 1.0f;
            app_->OnSettingsChanged();
            break;
        case ID_TOGGLE_CLICKTHROUGH:
            s.clickThrough = s.clickThrough > 0.5f ? 0.0f : 1.0f;
            app_->OnSettingsChanged();
            break;
        case ID_TOGGLE_TOPMOST:
            s.alwaysOnTop = s.alwaysOnTop > 0.5f ? 0.0f : 1.0f;
            app_->OnSettingsChanged();
            break;
        case ID_TOGGLE_CAPTURE:
            s.excludeFromCapture = s.excludeFromCapture > 0.5f ? 0.0f : 1.0f;
            app_->OnSettingsChanged();
            break;
        case ID_TOGGLE_OPAQUE:
            s.opaqueMode = s.opaqueMode > 0.5f ? 0.0f : 1.0f;
            app_->OnOpaqueModeChanged();
            break;
        case ID_TOGGLE_PAUSE:
            s.paused = s.paused > 0.5f ? 0.0f : 1.0f;
            app_->OnSettingsChanged();
            break;
        case ID_NEXT_PRESET:
            app_->SetPreset((s.presetIndex + 1) % static_cast<int>(presetCount));
            break;
        case ID_SETTINGS:
            app_->ToggleSettingsWindow();
            break;
        case ID_OPEN_CONFIG:
            ShellExecuteW(nullptr, L"open", Glow::ConfigDir().c_str(), nullptr,
                          nullptr, SW_SHOWNORMAL);
            break;
        case ID_ABOUT:
            MessageBoxW(hwnd_,
                        L"GlowOrb Desktop\n\n"
                        L"Native Win32 + DirectX 12 audio visualizer.\n"
                        L"WASAPI loopback captures all system audio (Spotify, YouTube,\n"
                        L"VLC, FL Studio, games, ...). No per-app integration needed.\n\n"
                        L"Ported from github.com/amazingcreationsltd/glow-orb",
                        L"About GlowOrb", MB_OK | MB_ICONINFORMATION);
            break;
        case ID_RESET:
            app_->ResetSettings();
            break;
        case ID_EXIT:
            app_->OnExit();
            break;
        default:
            break;
    }
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* self = reinterpret_cast<OverlayWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case Glow::WM_TRAY_CALLBACK:
            if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) {
                self->ShowContextMenu();
            } else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
                self->app_->ToggleSettingsWindow();
            } else if (LOWORD(lParam) == WM_LBUTTONUP) {
                self->app_->SetPreset(self->app_->GetSettings().presetIndex + 1);
            }
            return 0;

        case WM_COMMAND:
            self->OnCommand(LOWORD(wParam));
            return 0;

        case WM_NCHITTEST: {
            const Settings& s = self->app_->GetSettings();
            if (s.clickThrough > 0.5f && s.opaqueMode < 0.5f &&
                !self->app_->RendererIsOpaqueFallback()) {
                return HTTRANSPARENT;
            }
            return HTCAPTION;
        }

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_RBUTTONUP:
        case WM_NCRBUTTONUP:
            self->ShowContextMenu();
            return 0;

        case WM_LBUTTONDOWN:
            // WM_NCHITTEST already returns HTCAPTION when click-through is off,
            // so DefWindowProc performs the standard window drag.
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        case WM_MOVE: {
            Settings& s = self->app_->GetSettings();
            if (static_cast<OrbPosition>(static_cast<int>(s.position)) == OrbPosition::Custom) {
                RECT rc{};
                GetWindowRect(hwnd, &rc);
                s.customX = static_cast<float>(rc.left);
                s.customY = static_cast<float>(rc.top);
            }
            return 0;
        }

        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            self->Reposition();
            self->app_->OnDisplayChanged();
            return 0;

        case WM_HOTKEY:
            if (wParam == 1) {
                self->app_->SetPreset(self->app_->GetSettings().presetIndex + 1);
            }
            return 0;

        case WM_QUERYENDSESSION:
            self->app_->OnExit();
            return TRUE;

        case WM_KEYDOWN:
            if (wParam == 'P' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                self->app_->SetPreset(self->app_->GetSettings().presetIndex + 1);
            }
            return 0;

        case WM_DESTROY:
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
