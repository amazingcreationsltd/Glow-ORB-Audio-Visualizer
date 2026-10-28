#include "FFT.h"

FFT::FFT(uint32_t size) : size_(size) {
    // size must be a power of two
    uint32_t n = 1;
    levels_ = 0;
    while (n < size_) {
        n <<= 1;
        ++levels_;
    }
    size_ = n;

    reverse_.resize(size_);
    for (uint32_t i = 0; i < size_; ++i) {
        uint32_t r = 0;
        for (uint32_t b = 0; b < levels_; ++b) {
            r = (r << 1) | ((i >> b) & 1u);
        }
        reverse_[i] = r;
    }

    cosTable_.resize(size_ / 2);
    sinTable_.resize(size_ / 2);
    for (uint32_t i = 0; i < size_ / 2; ++i) {
        double a = 2.0 * 3.14159265358979323846 * static_cast<double>(i) / static_cast<double>(size_);
        cosTable_[i] = static_cast<float>(cos(a));
        sinTable_[i] = static_cast<float>(sin(a));
    }
}

void FFT::Transform(float* real, float* imag) const {
    // Bit-reversal permutation
    for (uint32_t i = 0; i < size_; ++i) {
        uint32_t j = reverse_[i];
        if (j > i) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
    }

    // Danielson-Lanczos section
    for (uint32_t len = 2; len <= size_; len <<= 1) {
        uint32_t half = len >> 1;
        uint32_t step = size_ / len;
        for (uint32_t i = 0; i < size_; i += len) {
            for (uint32_t j = 0, k = 0; j < half; ++j, k += step) {
                float c = cosTable_[k];
                float s = -sinTable_[k]; // forward FFT
                float tr = real[i + j + half] * c - imag[i + j + half] * s;
                float ti = real[i + j + half] * s + imag[i + j + half] * c;
                real[i + j + half] = real[i + j] - tr;
                imag[i + j + half] = imag[i + j] - ti;
                real[i + j] += tr;
                imag[i + j] += ti;
            }
        }
    }
}
