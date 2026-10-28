#include "Settings.h"
#include "Util.h"

namespace {

float GetF(const wchar_t* section, const wchar_t* key, float def, const std::wstring& path) {
    wchar_t buf[128] = {};
    GetPrivateProfileStringW(section, key, L"", buf, 128, path.c_str());
    if (buf[0] == 0) return def;
    return static_cast<float>(wcstod(buf, nullptr));
}

int GetI(const wchar_t* section, const wchar_t* key, int def, const std::wstring& path) {
    return GetPrivateProfileIntW(section, key, def, path.c_str());
}

void PutF(const wchar_t* section, const wchar_t* key, float v, const std::wstring& path) {
    wchar_t buf[128];
    swprintf(buf, 128, L"%.5f", v);
    WritePrivateProfileStringW(section, key, buf, path.c_str());
}

} // namespace

void Settings::Reset() {
    *this = Settings{};
}

void Settings::Load(const std::wstring& path) {
    const wchar_t* S = L"GlowOrb";
    presetIndex        = GetI(S, L"preset", 0, path);
    gain               = GetF(S, L"gain", 3.5f, path);
    orbSize            = GetF(S, L"orbSize", 560.0f, path);
    sizePulse          = GetF(S, L"sizePulse", 0.28f, path);
    glowIntensity      = GetF(S, L"glow", 0.9f, path);
    haloNoise          = GetF(S, L"haloNoise", 0.5f, path);
    opacity            = GetF(S, L"opacity", 1.0f, path);
    particleGlow       = GetF(S, L"particleGlow", 1.0f, path);
    particleDensity    = GetF(S, L"particleDensity", 520.0f, path);
    particleSpeed      = GetF(S, L"particleSpeed", 1.0f, path);
    particleSpread     = GetF(S, L"particleSpread", 50.0f, path);
    particlesEnabled   = GetF(S, L"particles", 1.0f, path);
    clickThrough       = GetF(S, L"clickThrough", 1.0f, path);
    alwaysOnTop        = GetF(S, L"alwaysOnTop", 1.0f, path);
    excludeFromCapture = GetF(S, L"excludeFromCapture", 0.0f, path);
    opaqueMode         = GetF(S, L"opaqueMode", 0.0f, path);
    position           = GetF(S, L"position", 0.0f, path);
    monitorIndex       = GetF(S, L"monitor", 0.0f, path);
    customX            = GetF(S, L"customX", 0.0f, path);
    customY            = GetF(S, L"customY", 0.0f, path);
    stateMode          = GetF(S, L"stateMode", 0.0f, path);
    fpsLimit           = GetF(S, L"fpsLimit", 0.0f, path);

    wchar_t device[512] = {};
    GetPrivateProfileStringW(S, L"deviceId", L"", device, 512, path.c_str());
    deviceId = device;

    // Clamp everything to sane values (also protects a hand-edited INI).
    orbSize = GlowClamp(orbSize, 200.0f, 1400.0f);
    gain = GlowClamp(gain, 0.5f, 12.0f);
    opacity = GlowClamp(opacity, 0.1f, 1.0f);
    particleDensity = GlowClamp(particleDensity, 0.0f, 1400.0f);
    particleSpread = GlowClamp(particleSpread, 5.0f, 200.0f);
    particleSpeed = GlowClamp(particleSpeed, 0.1f, 5.0f);
    sizePulse = GlowClamp(sizePulse, 0.0f, 0.8f);
    glowIntensity = GlowClamp(glowIntensity, 0.0f, 2.0f);
    haloNoise = GlowClamp(haloNoise, 0.0f, 1.0f);
    presetIndex = static_cast<int>(GlowClamp(static_cast<float>(presetIndex), 0.0f, 100.0f));
    position = GlowClamp(position, 0.0f, static_cast<float>(OrbPosition::Count) - 1.0f);
}

void Settings::Save(const std::wstring& path) const {
    const wchar_t* S = L"GlowOrb";
    WritePrivateProfileStringW(S, L"preset", std::to_wstring(presetIndex).c_str(), path.c_str());
    PutF(S, L"gain", gain, path);
    PutF(S, L"orbSize", orbSize, path);
    PutF(S, L"sizePulse", sizePulse, path);
    PutF(S, L"glow", glowIntensity, path);
    PutF(S, L"haloNoise", haloNoise, path);
    PutF(S, L"opacity", opacity, path);
    PutF(S, L"particleGlow", particleGlow, path);
    PutF(S, L"particleDensity", particleDensity, path);
    PutF(S, L"particleSpeed", particleSpeed, path);
    PutF(S, L"particleSpread", particleSpread, path);
    PutF(S, L"particles", particlesEnabled, path);
    PutF(S, L"clickThrough", clickThrough, path);
    PutF(S, L"alwaysOnTop", alwaysOnTop, path);
    PutF(S, L"excludeFromCapture", excludeFromCapture, path);
    PutF(S, L"opaqueMode", opaqueMode, path);
    PutF(S, L"position", position, path);
    PutF(S, L"monitor", monitorIndex, path);
    PutF(S, L"customX", customX, path);
    PutF(S, L"customY", customY, path);
    PutF(S, L"stateMode", stateMode, path);
    PutF(S, L"fpsLimit", fpsLimit, path);
    WritePrivateProfileStringW(S, L"deviceId", deviceId.c_str(), path.c_str());
}
