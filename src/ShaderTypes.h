#pragma once
#include <cstdint>

// Mirror of the cbuffer in shaders/Orb.hlsl. Must stay 256-byte aligned.
// Layout is deliberately all float2/float4 so HLSL and C++ agree exactly.
struct GlowVec2 { float x, y; };
struct GlowVec4 { float x, y, z, w; };

struct OrbConstants {
    GlowVec2 uResolution;  // actual client size in pixels
    float    uSize;        // virtual size used for orb math (smaller than window -> halo room)
    float    uLevel;       // smoothed RMS level 0..1

    GlowVec4 uParams0;     // warp, ridge, sharp, zoom
    GlowVec4 uParams1;     // exposure, glow, rim, hear
    GlowVec4 uParams2;     // fade, bass, mid, treble
    GlowVec4 uParams3;     // beat, haloAngle, haloNoise, particleGlow
    GlowVec4 uParams4;     // phase, voice, opaque, time
    GlowVec4 uParams5;     // orbScale, spare, spare, spare

    GlowVec4 uColA, uColB, uColC, uColD;
    GlowVec4 uHi, uCool, uWarm, uGlowCol;
    GlowVec4 uHalo[5];     // conic-gradient halo stops
    GlowVec4 uPad[12];     // pad to 512 bytes
};

static_assert(sizeof(OrbConstants) == 512, "OrbConstants must be 512 bytes (2 * 256)");

// One instance per particle, uploaded into a structured buffer each frame.
struct ParticleGPU {
    GlowVec2 pos;     // NDC center
    GlowVec2 extent;  // NDC half-size of the quad
    GlowVec4 color;   // rgb + alpha
};
static_assert(sizeof(ParticleGPU) == 32, "ParticleGPU must be 32 bytes");

static const uint32_t WAVE_SIZE = 256;
static const uint32_t MAX_PARTICLES = 1400;
static const uint32_t FRAME_COUNT = 3;
