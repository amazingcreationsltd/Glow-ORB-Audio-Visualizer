#pragma once
#include "Common.h"

namespace Glow {

std::wstring ExeDir();
std::wstring ConfigDir();
std::wstring ConfigFilePath();
std::wstring LogFilePath();
std::wstring FindShaderFile();

std::string  Narrow(const std::wstring& s);
std::wstring Widen(const std::string& s);
std::wstring HrToString(HRESULT hr);

void Log(const wchar_t* fmt, ...);
void Fatal(const wchar_t* title, const wchar_t* fmt, ...);

struct MonitorRect {
    int left = 0, top = 0, right = 0, bottom = 0;
    bool primary = false;
    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
};
std::vector<MonitorRect> GetMonitors();

void EnableDpiAwareness();

double NowSeconds();

} // namespace Glow
