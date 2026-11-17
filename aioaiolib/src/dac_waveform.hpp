#pragma once

#include "AIOAIO.h"
#include "backend.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace aioaio {
// The owning Board serializes start/stop. Only the refill worker uses the
// backend while running; stop may concurrently cancel its blocking IRQ wait.
class DacWaveform {
public:
    explicit DacWaveform(Backend &backend) : hw_(backend) {
        status_.StructSize = sizeof(status_);
        status_.State = AIO_STATE_IDLE;
    }
    ~DacWaveform() { stop(); }
    int start(uint32_t channels, uint32_t count, const uint16_t *samples,
              double point_rate, uint32_t mode);
    int stop() noexcept;
    // Owning Board calls this only after stop() joined the worker and hardware reset succeeded.
    void reset_status();
    bool running();
    TDACWaveformStatus status();

private:
    using Clock = std::chrono::steady_clock;
    Backend &hw_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> checking_{false};
    bool done_ = true;
    TDACWaveformStatus status_{};
    Clock::time_point started_{};
    std::vector<uint16_t> samples_;
    std::vector<uint32_t> upload_;
    size_t position_ = 0;
    uint32_t channels_ = 0, fifo_samples_ = 0, mode_ = 0;
    std::chrono::microseconds poll_interval_{100};
    uint64_t elapsed() const;
    int fill(uint32_t count);
    void run() noexcept;
    void finish(int error) noexcept;
};
} // namespace aioaio
