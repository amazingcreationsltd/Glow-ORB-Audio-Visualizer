#pragma once
// -----------------------------------------------------------------------------
// GlowOrb Desktop - shared Win32 / COM / DX12 plumbing
// -----------------------------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 // Windows 10
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000000
#endif

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <propvarutil.h>
#include <propsys.h>
#include <commctrl.h>

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <dcomp.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace Glow {

// Logging is implemented in Util.cpp. LogIfFailed() returns true on success.
bool LogIfFailed(HRESULT hr, const char* expr, const char* file, int line);

// Minimal COM smart pointer (no WRL / no external dependency).
template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(std::nullptr_t) {}
    ~ComPtr() { ReleaseInternal(); }

    ComPtr(const ComPtr& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }

    ComPtr& operator=(const ComPtr& o) {
        if (this != &o) {
            T* next = o.p_;
            if (next) next->AddRef();
            ReleaseInternal();
            p_ = next;
        }
        return *this;
    }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) {
            ReleaseInternal();
            p_ = o.p_;
            o.p_ = nullptr;
        }
        return *this;
    }
    ComPtr& operator=(std::nullptr_t) { ReleaseInternal(); return *this; }

    T* Get() const { return p_; }
    T* const* GetAddressOf() const { return &p_; }
    T** GetAddressOf() { ReleaseInternal(); return &p_; }
    void** VoidAddressOf() { return reinterpret_cast<void**>(GetAddressOf()); }
    T* Detach() { T* t = p_; p_ = nullptr; return t; }
    void Reset() { ReleaseInternal(); }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    bool operator!() const { return p_ == nullptr; }

    template <class U>
    bool As(ComPtr<U>& out) const {
        if (!p_) return false;
        return SUCCEEDED(p_->QueryInterface(__uuidof(U), out.VoidAddressOf()));
    }

private:
    void ReleaseInternal() {
        if (p_) {
            p_->Release();
            p_ = nullptr;
        }
    }
    T* p_ = nullptr;
};

} // namespace Glow

using Glow::ComPtr;

// Usage: D3D12CreateDevice(adapter, level, GLOW_PPV(ID3D12Device, m_device));
#define GLOW_PPV(iface, cp) __uuidof(iface), reinterpret_cast<void**>((cp).GetAddressOf())

#define HR_RETURN_FALSE(expr)                                                        \
    do {                                                                             \
        if (!Glow::LogIfFailed((expr), #expr, __FILE__, __LINE__)) return false;     \
    } while (0)

#define HR_RETURN(expr)                                                              \
    do {                                                                             \
        if (!Glow::LogIfFailed((expr), #expr, __FILE__, __LINE__)) return;           \
    } while (0)

#define HR_RETURN_HR(expr)                                                           \
    do {                                                                             \
        HRESULT glowHr_ = (expr);                                                    \
        if (FAILED(glowHr_)) {                                                       \
            Glow::LogIfFailed(glowHr_, #expr, __FILE__, __LINE__);                  \
            return glowHr_;                                                          \
        }                                                                            \
    } while (0)

inline float GlowClamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
inline float GlowLerp(float a, float b, float t) { return a + (b - a) * t; }

#ifndef PI
#define PI 3.14159265358979323846f
#endif
