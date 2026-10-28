#pragma once
#include "Common.h"
#include "MenuIds.h"

class App;

// Modeless Win32 control panel (no .rc / no framework). Live-edits Settings.
class SettingsWindow {
public:
    SettingsWindow() = default;
    ~SettingsWindow() { Destroy(); }

    bool Create(HINSTANCE instance, App* app);
    void Destroy();
    void Show();
    void Hide();
    bool Visible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    void Refresh();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    struct Slider {
        HWND bar = nullptr;
        HWND value = nullptr;
        float* target = nullptr;
        float min = 0.0f, max = 1.0f;
        int id = 0;
        bool integer = false;
    };

    HWND MakeLabel(const wchar_t* text, int x, int y, int w, int h, int id);
    HWND MakeCombo(int x, int y, int w, int h, int id);
    HWND MakeCheck(const wchar_t* text, int x, int y, int w, int h, int id);
    HWND MakeButton(const wchar_t* text, int x, int y, int w, int h, int id);
    void AddSlider(const wchar_t* label, float* target, float min, float max,
                   bool integer, int y);

    void OnCommand(WPARAM wParam, LPARAM lParam);
    void OnScroll(HWND bar);
    void UpdateSliderLabel(const Slider& s);
    void FillCombos();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    App* app_ = nullptr;
    HFONT font_ = nullptr;
    HWND presetCombo_ = nullptr, deviceCombo_ = nullptr, positionCombo_ = nullptr;
    HWND stateCombo_ = nullptr, monitorCombo_ = nullptr;
    std::vector<HWND> checks_;
    std::vector<Slider> sliders_;
    std::vector<std::wstring> deviceIds_;
};
