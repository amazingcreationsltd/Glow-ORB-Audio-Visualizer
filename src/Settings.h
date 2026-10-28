#pragma once
#include "Common.h"

// Screen anchor for the overlay window.
enum class OrbPosition : int {
    Center = 0,
    BottomCenter,
    TopCenter,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    Custom,
    Count
};

struct Settings {
    int      presetIndex = 0;
    float    gain = 3.5f;
    float    orbSize = 560.0f;
    float    sizePulse = 0.28f;
    float    glowIntensity = 0.9f;
    float    haloNoise = 0.5f;
    float    opacity = 1.0f;

    float    particleGlow = 1.0f;
    float    particleDensity = 520.0f;
    float    particleSpeed = 1.0f;
    float    particleSpread = 50.0f;
    float    particlesEnabled = 1.0f;

    float    clickThrough = 1.0f;
    float    alwaysOnTop = 1.0f;
    float    excludeFromCapture = 0.0f;
    float    paused = 0.0f;
    float    opaqueMode = 0.0f;  // 0 = transparent DComp overlay, 1 = normal opaque window

    float    position = 0.0f;
    float    monitorIndex = 0.0f;
    float    customX = 0.0f;
    float    customY = 0.0f;
    float    stateMode = 0.0f;   // 0 auto, 1 idle, 2 speaking, 3 listening, 4 thinking
    float    fpsLimit = 0.0f;    // 0 = vsync

    std::wstring deviceId;       // empty = follow Windows default output

    void Load(const std::wstring& path);
    void Save(const std::wstring& path) const;
    void Reset();
};
