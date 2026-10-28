#pragma once
#include "Common.h"
#include "ShaderTypes.h"

namespace Glow {

// Direct port of buildPalette()/muteRgb()/rotateHue() from the web demo.
struct Rgb { float r = 0, g = 0, b = 0; };  // 0..1

struct Palette {
    GlowVec4 a, b, c, d, hi, cool, warm, glow;
    GlowVec4 halo[5];
};

Rgb FromHex(uint32_t hex);
GlowVec4 ToVec4(const Rgb& c);

Palette BuildPalette(uint32_t fromHex, uint32_t toHex, float mute,
                     const uint32_t haloHex[5]);

} // namespace Glow
