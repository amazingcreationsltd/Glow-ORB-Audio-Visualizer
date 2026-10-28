#pragma once
#include "Common.h"
#include "RingBuffer.h"
#include "FFT.h"
#include "ShaderTypes.h"

// Turns the WASAPI loopback stream into the exact signals the WebGL demo used:
// a smoothed RMS level, a 256-tap time-domain waveform, plus FFT bands/beat.
class AudioAnalyzer {
public:
    explicit AudioAnalyzer(RingBuffer& ring);

    struct Frame {
        float level = 0.0f;     // uLevel  (0..1)
        float rms = 0.0f;
        float bass = 0.0f;      // 0..1
        float mid = 0.0f;
        float treble = 0.0f;
        float beat = 0.0f;      // 0..1 pulse
        bool  hasAudio = false;
        float waveform[WAVE_SIZE] = {};  // -1..1, uploaded to uWaveTex
    };

    void Update(double dt, float sampleRate, float gain, Frame& out);
    void Reset();

private:
    RingBuffer& ring_;
    FFT fft_;
    uint32_t n_ = 1024;
    std::vector<float> samples_;
    std::vector<float> real_;
    std::vector<float> imag_;
    std::vector<float> window_;
    std::vector<float> magnitudes_;
    std::vector<float> previousMagnitudes_;
    std::vector<float> fluxHistory_;
    size_t fluxCursor_ = 0;
    uint64_t lastTotal_ = 0;

    float level_ = 0.0f;
    float bass_ = 0.0f;
    float mid_ = 0.0f;
    float treble_ = 0.0f;
    float beat_ = 0.0f;
};
