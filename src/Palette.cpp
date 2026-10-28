#include "Palette.h"

namespace Glow {
namespace {

Rgb Mix(const Rgb& a, const Rgb& b, float t) {
    return { GlowLerp(a.r, b.r, t), GlowLerp(a.g, b.g, t), GlowLerp(a.b, b.b, t) };
}

Rgb ClampRgb(Rgb c) {
    return { GlowClamp(c.r, 0.0f, 1.0f), GlowClamp(c.g, 0.0f, 1.0f), GlowClamp(c.b, 0.0f, 1.0f) };
}

// Same hue-rotation matrix the JS buildPalette() uses (YIQ-style rotation).
Rgb RotateHue(const Rgb& in, float deg) {
    const float ang = deg * 3.14159265358979323846f / 180.0f;
    const float c = cosf(ang), s = sinf(ang);
    const float k = 1.0f / 3.0f;
    const float q = sqrtf(k);
    const float m0 = c + (1.0f - c) * k;
    const float m1 = k * (1.0f - c) - q * s;
    const float m2 = k * (1.0f - c) + q * s;
    return ClampRgb({
        in.r * m0 + in.g * m1 + in.b * m2,
        in.r * m2 + in.g * m0 + in.b * m1,
        in.r * m1 + in.g * m2 + in.b * m0
    });
}

Rgb Mute(const Rgb& c, float amount) {
    const float lum = c.r * 0.2126f + c.g * 0.7152f + c.b * 0.0722f;
    return Mix(c, { lum, lum, lum }, amount);
}

GlowVec4 V(const Rgb& c) { return { c.r, c.g, c.b, 1.0f }; }

} // namespace

Rgb FromHex(uint32_t hex) {
    return {
        static_cast<float>((hex >> 16) & 0xFFu) / 255.0f,
        static_cast<float>((hex >> 8) & 0xFFu) / 255.0f,
        static_cast<float>(hex & 0xFFu) / 255.0f
    };
}

GlowVec4 ToVec4(const Rgb& c) { return V(c); }

Palette BuildPalette(uint32_t fromHex, uint32_t toHex, float mute,
                     const uint32_t haloHex[5]) {
    const Rgb from = FromHex(fromHex);
    const Rgb to = FromHex(toHex);
    const Rgb white{ 1.0f, 1.0f, 1.0f };

    const Rgb a = Mute(from, mute);
    const Rgb b = Mute(to, mute);
    const Rgb c = Mute(RotateHue(Mix(from, to, 0.35f), -38.0f), mute);
    const Rgb d = Mute(RotateHue(Mix(from, to, 0.65f), 42.0f), mute);
    const Rgb mid = Mix(a, b, 0.5f);

    Palette p{};
    p.a = V(a);
    p.b = V(b);
    p.c = V(c);
    p.d = V(d);
    p.hi = V(Mix(mid, white, 0.82f));
    p.cool = V(Mix(a, white, 0.15f));
    p.warm = V(Mix(d, white, 0.10f));
    p.glow = V(mid);
    for (uint32_t i = 0; i < 5; ++i) {
        p.halo[i] = V(FromHex(haloHex[i]));
    }
    return p;
}

} // namespace Glow
