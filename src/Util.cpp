#include "Util.h"

namespace Glow {
namespace {

std::wstring g_exeDir;
std::wstring g_configDir;

std::mutex& LogMutex() {
    static std::mutex m;
    return m;
}

void AppendLogLine(const wchar_t* text) {
    std::lock_guard<std::mutex> lock(LogMutex());
    OutputDebugStringW(text);
    OutputDebugStringW(L"\n");

    std::wstring path = LogFilePath();
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    std::string utf8 = Narrow(text) + "\r\n";
    DWORD written = 0;
    WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(h);
}

std::wstring SystemFolder(int csidl) {
    wchar_t buf[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, buf))) {
        return std::wstring(buf);
    }
    return L".";
}

} // namespace

std::wstring ExeDir() {
    if (!g_exeDir.empty()) return g_exeDir;
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring full(buf, n);
    size_t slash = full.find_last_of(L"\\/");
    g_exeDir = (slash == std::wstring::npos) ? std::wstring(L".") : full.substr(0, slash);
    return g_exeDir;
}

std::wstring ConfigDir() {
    if (!g_configDir.empty()) return g_configDir;
    std::wstring dir = SystemFolder(CSIDL_APPDATA) + L"\\GlowOrb";
    CreateDirectoryW(dir.c_str(), nullptr);
    g_configDir = dir;
    return g_configDir;
}

std::wstring ConfigFilePath() { return ConfigDir() + L"\\settings.ini"; }
std::wstring LogFilePath() { return ConfigDir() + L"\\gloworb.log"; }

std::wstring FindShaderFile() {
    const wchar_t* names[] = { L"\\shaders\\Orb.hlsl", L"\\Orb.hlsl" };
    for (const wchar_t* n : names) {
        std::wstring p = ExeDir() + n;
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    }
    std::wstring cwdPath = L"shaders\\Orb.hlsl";
    if (GetFileAttributesW(cwdPath.c_str()) != INVALID_FILE_ATTRIBUTES) return cwdPath;
    return L"";
}

std::string Narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::wstring HrToString(HRESULT hr) {
    wchar_t* msg = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, static_cast<DWORD>(hr), 0,
                             reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    wchar_t code[64];
    swprintf(code, 64, L" (0x%08lX)", static_cast<unsigned long>(static_cast<uint32_t>(hr)));
    std::wstring out;
    if (n && msg) {
        out.assign(msg, n);
        while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n')) out.pop_back();
    } else {
        out = L"Unknown error";
    }
    if (msg) LocalFree(msg);
    return out + code;
}

bool LogIfFailed(HRESULT hr, const char* expr, const char* file, int line) {
    if (SUCCEEDED(hr)) return true;
    std::wstring text = L"[GlowOrb] FAILED: " + Widen(expr) + L" -> " + HrToString(hr) +
                        L" at " + Widen(file) + L":" + std::to_wstring(line);
    AppendLogLine(text.c_str());
    return false;
}

void Log(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(buf, 2047, fmt, ap);
    buf[2047] = 0;
    va_end(ap);
    AppendLogLine(buf);
}

void Fatal(const wchar_t* title, const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(buf, 2047, fmt, ap);
    buf[2047] = 0;
    va_end(ap);
    Log(L"[FATAL] %s", buf);
    MessageBoxW(nullptr, buf, title, MB_OK | MB_ICONERROR);
}

struct EnumCtx {
    std::vector<MonitorRect>* out;
};

static BOOL CALLBACK MonitorProc(HMONITOR hmon, HDC, LPRECT, LPARAM lp) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lp);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hmon, &mi)) {
        MonitorRect r;
        r.left = mi.rcMonitor.left;
        r.top = mi.rcMonitor.top;
        r.right = mi.rcMonitor.right;
        r.bottom = mi.rcMonitor.bottom;
        r.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        ctx->out->push_back(r);
    }
    return TRUE;
}

std::vector<MonitorRect> GetMonitors() {
    std::vector<MonitorRect> result;
    EnumCtx ctx{ &result };
    EnumDisplayMonitors(nullptr, nullptr, MonitorProc, reinterpret_cast<LPARAM>(&ctx));
    if (result.empty()) {
        MonitorRect r;
        r.right = GetSystemMetrics(SM_CXSCREEN);
        r.bottom = GetSystemMetrics(SM_CYSCREEN);
        r.primary = true;
        result.push_back(r);
    }
    return result;
}

void EnableDpiAwareness() {
    using Fn = BOOL(WINAPI*)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto fn = reinterpret_cast<Fn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4
        if (fn && fn(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)))) return;
    }
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        using Fn2 = HRESULT(WINAPI*)(int);
        auto fn2 = reinterpret_cast<Fn2>(GetProcAddress(shcore, "SetProcessDpiAwareness"));
        if (fn2) fn2(2); // PROCESS_PER_MONITOR_DPI_AWARE
        FreeLibrary(shcore);
        return;
    }
    SetProcessDPIAware();
}

double NowSeconds() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) / static_cast<double>(freq.QuadPart);
}

} // namespace Glow
