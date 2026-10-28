#include "ParticleSystem.h"

namespace {

float HueToRgb(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f / 2.0f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

GlowVec4 Hsv(float h, float s, float v, float a) {
    float r = v, g = v, b = v;
    if (s > 0.0f) {
        const float q = v < 0.5f ? v * (1.0f + s) : v + s - v * s;
        const float p = 2.0f * v - q;
        r = HueToRgb(p, q, h + 1.0f / 3.0f);
        g = HueToRgb(p, q, h);
        b = HueToRgb(p, q, h - 1.0f / 3.0f);
    }
    return { r, g, b, a };
}

} // namespace

float ParticleSystem::Random() {
    // xorshift32 - deterministic, fast, no <random> overhead
    uint32_t x = rngState_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState_ = x;
    return static_cast<float>(x & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

ParticleSystem::Star ParticleSystem::MakeStar(float edge) {
    static const float hues[] = { 218.0f, 232.0f, 248.0f, 260.0f, 275.0f, 290.0f };
    Star s{};
    s.angle = Random() * 2.0f * 3.14159265358979323846f;
    s.radius = edge + Random() * 2.0f;
    s.radialSpeed = 4.5f + Random() * 6.5f;
    s.angularSpeed = (Random() < 0.5f ? -1.0f : 1.0f) * (0.035f + Random() * 0.11f);
    s.size = (Random() < 0.07f) ? (0.85f + Random() * 0.65f)
                                : (0.20f + Random() * 0.48f);
    s.alpha = 0.12f + Random() * 0.42f;
    s.phase = Random() * 2.0f * 3.14159265358979323846f;
    s.hue = hues[static_cast<uint32_t>(Random() * 6.0f) % 6u];
    s.age = 0.0f;
    s.life = 2.5f + Random() * 4.5f;
    return s;
}

void ParticleSystem::Emit(uint32_t count, float edge) {
    for (uint32_t i = 0; i < count; ++i) stars_.push_back(MakeStar(edge));
}

void ParticleSystem::Clear() {
    stars_.clear();
    gpu_.clear();
    previousAudio_ = 0.0f;
    lastEmission_ = -1.0;
}

void ParticleSystem::Update(double dt, const AudioAnalyzer::Frame& frame,
                            const Settings& settings, uint32_t width, uint32_t height,
                            float orbScale, float virtualSize) {
    time_ += dt;
    const float d = static_cast<float>(std::min(0.05, std::max(0.0, dt)));
    gpu_.clear();

    if (settings.particlesEnabled < 0.5f) {
        Clear();
        return;
    }

    const int density = static_cast<int>(std::min(MAX_PARTICLES, static_cast<uint32_t>(settings.particleDensity)));
    const float speed = std::max(0.1f, settings.particleSpeed);
    const float spread = settings.particleSpread;
    const float glow = settings.particleGlow;

    if (static_cast<int>(stars_.size()) > density) stars_.resize(static_cast<size_t>(density));

    const float edge = 0.86f * virtualSize * 0.5f; // orb rim in pixels
    const float outerEdge = edge + spread;

    const float audio = frame.level;
    const float rise = audio - previousAudio_;
    previousAudio_ = audio;

    const bool isHit = audio > 0.085f && rise > 0.018f &&
                       (time_ - lastEmission_) > 0.055;
    if (isHit && static_cast<int>(stars_.size()) < density) {
        const float strength = GlowClamp((rise - 0.018f) / 0.16f, 0.0f, 1.0f);
        const int burst = std::max(2, static_cast<int>(3.0f + strength * 15.0f));
        const int available = density - static_cast<int>(stars_.size());
        Emit(static_cast<uint32_t>(std::min(burst, available)), edge);
        lastEmission_ = time_;
    }

    const float halfW = std::max(1.0f, static_cast<float>(width) * 0.5f);
    const float halfH = std::max(1.0f, static_cast<float>(height) * 0.5f);
    const float denom = std::min(24.0f, spread * 0.48f);

    for (size_t i = 0; i < stars_.size();) {
        Star& s = stars_[i];
        s.age += d / s.life;
        s.angle += s.angularSpeed * d * speed;
        s.radius += s.radialSpeed * d * speed;

        if (s.age >= 1.0f || s.radius > outerEdge) {
            stars_[i] = stars_.back();
            stars_.pop_back();
            continue;
        }
        ++i;

        const float innerFade = GlowClamp((s.radius - edge) / 3.0f, 0.0f, 1.0f);
        const float outerFade = GlowClamp((outerEdge - s.radius) / denom, 0.0f, 1.0f);
        const float lifeFade = sinf(3.14159265358979323846f * std::min(1.0f, s.age));
        const float twinkle = 0.72f + 0.28f * sinf(static_cast<float>(time_) * 1.2f + s.phase);
        const float alpha = std::min(1.0f, s.alpha * innerFade * outerFade *
                                              lifeFade * twinkle * glow);
        if (alpha < 0.008f) continue;

        const float r = s.radius * orbScale;
        const float size = s.size * (0.92f + (orbScale - 1.0f) * 0.22f);
        const float bloom = size * (s.size > 1.0f ? 5.2f : 3.4f) * (0.65f + glow * 0.35f);

        ParticleGPU p{};
        p.pos.x = cosf(s.angle) * r / halfW;
        p.pos.y = sinf(s.angle) * r / halfH;
        p.extent.x = bloom / halfW;
        p.extent.y = bloom / halfH;
        p.color = Hsv(s.hue / 360.0f, 0.92f, 0.96f, std::min(0.85f, alpha * 1.15f));
        gpu_.push_back(p);
        if (gpu_.size() >= MAX_PARTICLES) break;
    }
}
