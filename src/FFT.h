#pragma once
#include "Common.h"

// Small dependency-free radix-2 Cooley-Tukey FFT (in-place, real/imaginary split).
class FFT {
public:
    explicit FFT(uint32_t size = 1024);

    void Transform(float* real, float* imag) const;
    uint32_t Size() const { return size_; }

private:
    uint32_t size_ = 0;
    uint32_t levels_ = 0;
    std::vector<uint32_t> reverse_;
    std::vector<float> cosTable_;
    std::vector<float> sinTable_;
};
