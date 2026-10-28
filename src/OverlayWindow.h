#pragma once
#include "Common.h"
#include "MenuIds.h"
#include "Settings.h"

class App;

// Layered, click-through, always-on-top transparent window + tray icon.
class OverlayWindow {
public:
    OverlayWindow() = default;
    ~OverlayWindow() { Destroy(); }

    bool Create(HINSTANCE instance, App* app);
    void Destroy();

    HWND Hwnd() const { return hwnd_; }
    HICON Icon() const { return icon_; }

    void ApplyStyles();
    void Reposition();
    void Resize(uint32_t size);
    void UpdateTrayTip(const std::wstring& text);
    void ShowContextMenu();
    void RefreshTrayMenu();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void OnCommand(UINT id);
    void AddTrayIcon();
    void RemoveTrayIcon();
    HMENU BuildMenu();
    void ApplyMenuChecks(HMENU menu);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HICON icon_ = nullptr;
    App* app_ = nullptr;
    NOTIFYICONDATAW tray_ = {};
    bool trayAdded_ = false;
    bool iconOwned_ = false;
};
