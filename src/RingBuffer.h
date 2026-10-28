#pragma once
#include "Common.h"

// Thread-safe SPSC circular buffer of mono float samples.
// The WASAPI thread is the only writer; the render thread is the only reader.
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity = 1u << 16)
        : buffer_(capacity, 0.0f), capacity_(capacity) {}

    void Write(const float* src, size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < count; ++i) {
            buffer_[written_ % capacity_] = src[i];
            ++written_;
        }
    }

    void WriteZeros(size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < count; ++i) {
            buffer_[written_ % capacity_] = 0.0f;
            ++written_;
        }
    }

    // Copies the newest `count` samples into dst with the NEWEST sample at
    // dst[count-1]. Zero-pads the front if fewer samples exist.
    size_t SnapshotLast(float* dst, size_t count) const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t available = static_cast<size_t>(
            std::min<uint64_t>(static_cast<uint64_t>(count), written_));
        const size_t offset = count - available;
        for (size_t i = 0; i < offset; ++i) dst[i] = 0.0f;
        uint64_t start = written_ - available;
        for (size_t i = 0; i < available; ++i) {
            dst[offset + i] = buffer_[static_cast<size_t>((start + i) % capacity_)];
        }
        return available;
    }

    uint64_t TotalWritten() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return written_;
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        written_ = 0;
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    }

private:
    mutable std::mutex mutex_;
    std::vector<float> buffer_;
    size_t capacity_ = 0;
    uint64_t written_ = 0;
};
