#include "AudioAnalyzer.h"

AudioAnalyzer::AudioAnalyzer(RingBuffer& ring)
    : ring_(ring),
      fft_(1024),
      n_(fft_.Size()),
      samples_(n_, 0.0f),
      real_(n_, 0.0f),
      imag_(n_, 0.0f),
      window_(n_, 0.0f),
      magnitudes_(n_ / 2, 0.0f),
      previousMagnitudes_(n_ / 2, 0.0f),
      fluxHistory_(43, 0.0f) {
    for (uint32_t i = 0; i < n_; ++i) {
        window_[i] = 0.5f - 0.5f * cosf(2.0f * 3.14159265358979323846f *
                                        static_cast<float>(i) / static_cast<float>(n_ - 1));
    }
}

void AudioAnalyzer::Reset() {
    level_ = bass_ = mid_ = treble_ = beat_ = 0.0f;
    lastTotal_ = 0;
    std::fill(previousMagnitudes_.begin(), previousMagnitudes_.end(), 0.0f);
    std::fill(fluxHistory_.begin(), fluxHistory_.end(), 0.0f);
}

void AudioAnalyzer::Update(double dt, float sampleRate, float gain, Frame& out) {
    const float d = static_cast<float>(std::min(0.1, std::max(0.0, dt)));
    const uint64_t total = ring_.TotalWritten();
    const bool hasNewSamples = (total != lastTotal_);
    lastTotal_ = total;

    std::fill(samples_.begin(), samples_.end(), 0.0f);
    ring_.SnapshotLast(samples_.data(), n_);
    if (!hasNewSamples) {
        // WASAPI loopback delivers no packets while the system is silent.
        // Treat stale buffers as silence so the visualizer relaxes to idle.
        std::fill(samples_.begin(), samples_.end(), 0.0f);
    }

    // --- RMS level, matching the JS: sqrt(sum(x^2)/N) * gain, smoothed -----
    double sumSquares = 0.0;
    for (uint32_t i = 0; i < n_; ++i) {
        const float s = samples_[i];
        sumSquares += static_cast<double>(s) * s;
    }
    const float rms = sqrtf(static_cast<float>(sumSquares / n_));
    const float target = hasNewSamples
        ? std::min(1.0f, rms * gain)
        : 0.0f;
    const float response = (target > level_) ? 22.0f : 7.0f;
    level_ += (target - level_) * (1.0f - expf(-d * response));

    // --- Waveform texture (256 taps) ---------------------------------------
    const uint32_t waveOffset = n_ - WAVE_SIZE;
    for (uint32_t i = 0; i < WAVE_SIZE; ++i) {
        out.waveform[i] = GlowClamp(samples_[waveOffset + i], -1.0f, 1.0f);
    }

    // --- FFT + bands --------------------------------------------------------
    for (uint32_t i = 0; i < n_; ++i) {
        real_[i] = samples_[i] * window_[i];
        imag_[i] = 0.0f;
    }
    fft_.Transform(real_.data(), imag_.data());

    const uint32_t bins = n_ / 2;
    const float sr = (sampleRate > 1000.0f) ? sampleRate : 48000.0f;
    auto binOf = [&](float hz) -> uint32_t {
        int b = static_cast<int>(hz * static_cast<float>(n_) / sr);
        const int clamped = std::min(static_cast<int>(bins - 1), std::max(1, b));
        return static_cast<uint32_t>(clamped);
    };
    const uint32_t bBassLo = binOf(20.0f),   bBassHi = binOf(250.0f);
    const uint32_t bMidLo = binOf(250.0f),   bMidHi = binOf(2000.0f);
    const uint32_t bTreLo = binOf(2000.0f),  bTreHi = binOf(12000.0f);

    float flux = 0.0f;
    for (uint32_t k = 1; k < bins; ++k) {
        const float mag = sqrtf(real_[k] * real_[k] + imag_[k] * imag_[k]) *
                          (2.0f / static_cast<float>(n_));
        magnitudes_[k] = mag;
        const float diff = mag - previousMagnitudes_[k];
        if (diff > 0.0f) flux += diff;
        previousMagnitudes_[k] = mag;
    }

    auto bandEnergy = [&](uint32_t lo, uint32_t hi) {
        double sum = 0.0;
        uint32_t count = 0;
        for (uint32_t k = lo; k < std::max(hi, lo + 1); ++k) {
            sum += magnitudes_[k];
            ++count;
        }
        const float avg = static_cast<float>(sum / std::max(1u, count));
        return 1.0f - expf(-avg * 55.0f);  // perceptual-ish compression
    };

    const float tb = hasNewSamples ? bandEnergy(bBassLo, bBassHi) : 0.0f;
    const float tm = hasNewSamples ? bandEnergy(bMidLo, bMidHi) : 0.0f;
    const float tt = hasNewSamples ? bandEnergy(bTreLo, bTreHi) : 0.0f;
    auto smooth = [&](float& current, float targetValue) {
        const float rate = targetValue > current ? 18.0f : 6.0f;
        current += (targetValue - current) * (1.0f - expf(-d * rate));
    };
    smooth(bass_, tb);
    smooth(mid_, tm);
    smooth(treble_, tt);

    // --- Spectral-flux beat detector ---------------------------------------
    fluxHistory_[fluxCursor_] = flux;
    fluxCursor_ = (fluxCursor_ + 1) % fluxHistory_.size();
    float avgFlux = 0.0f;
    for (float v : fluxHistory_) avgFlux += v;
    avgFlux /= static_cast<float>(fluxHistory_.size());
    if (hasNewSamples && flux > avgFlux * 1.6f && flux > 0.035f) {
        beat_ = 1.0f;
    }
    beat_ *= expf(-d * 5.5f);

    out.level = GlowClamp(level_, 0.0f, 1.0f);
    out.rms = rms;
    out.bass = GlowClamp(bass_, 0.0f, 1.0f);
    out.mid = GlowClamp(mid_, 0.0f, 1.0f);
    out.treble = GlowClamp(treble_, 0.0f, 1.0f);
    out.beat = GlowClamp(beat_, 0.0f, 1.0f);
    out.hasAudio = hasNewSamples && rms > 0.0002f;
}
