#pragma once
#include "Common.h"
#include "AudioAnalyzer.h"
#include "Settings.h"
#include "ShaderTypes.h"

// Port of drawBinaryOrbits() from script.js: stardust spawned only on audio
// hits, drifting outward and fading with a twinkle. Uploaded as a structured
// buffer and drawn as additive instanced quads by Particles.hlsl entry points.
class ParticleSystem {
public:
    void Update(double dt, const AudioAnalyzer::Frame& frame, const Settings& settings,
                uint32_t width, uint32_t height, float orbScale, float virtualSize);

    const ParticleGPU* Data() const { return gpu_.empty() ? nullptr : gpu_.data(); }
    uint32_t Count() const { return static_cast<uint32_t>(gpu_.size()); }
    void Clear();

private:
    struct Star {
        float angle, radius, radialSpeed, angularSpeed;
        float size, alpha, phase, hue, age, life;
    };

    float Random();
    Star MakeStar(float edge);
    void Emit(uint32_t count, float edge);

    std::vector<Star> stars_;
    std::vector<ParticleGPU> gpu_;
    uint32_t rngState_ = 0x9E3779B9u;
    float previousAudio_ = 0.0f;
    double lastEmission_ = -1.0;
    double time_ = 0.0;
};
