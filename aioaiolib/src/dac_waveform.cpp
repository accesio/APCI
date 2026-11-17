#include "dac_waveform.hpp"
#include <algorithm>
#include <cmath>
#include <new>
#include <system_error>

namespace aioaio {

uint64_t DacWaveform::elapsed() const {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - started_).count());
}

bool DacWaveform::running() {
    std::lock_guard<std::mutex> lock(mutex_);
    return !done_;
}

TDACWaveformStatus DacWaveform::status() {
    TDACWaveformStatus result;
    bool refresh;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result = status_;
        if (!done_) result.ElapsedNanoseconds = elapsed();
        refresh = !done_ && mode_ == AIO_DAC_REFILL_IRQ;
    }
    if (refresh) {
        // A lost/disabled interrupt must not hide an empty FIFO from clients
        // monitoring status. Only the worker reads hardware; this asks it to
        // recheck. A later query retries if cancellation preceded its wait.
        checking_.store(true);
        try { (void)hw_.cancel_irq(); } catch (...) {}
    }
    return result;
}

void DacWaveform::reset_status() {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = {};
    status_.StructSize = sizeof(status_);
    status_.State = AIO_STATE_IDLE;
}

int DacWaveform::fill(uint32_t count) {
    // Every upload contains complete channel groups. Wrapping the host
    // waveform does not change the next channel tag or split a sample word.
    count -= count % channels_;
    if (!count) return 0;
    if (count > upload_.size()) return EPROTO;
    size_t next = position_;
    for (uint32_t i = 0; i < count; ++i) {
        upload_[i] = (static_cast<uint32_t>(next % channels_) << 16) | samples_[next];
        if (++next == samples_.size()) next = 0;
    }
    int error = hw_.dac_upload(upload_.data(), count);
    if (error) return error;
    position_ = next;
    std::lock_guard<std::mutex> lock(mutex_);
    status_.UploadedSamples += count;
    ++status_.Refills;
    return 0;
}

int DacWaveform::start(uint32_t channels, uint32_t count, const uint16_t *samples,
                       double point_rate, uint32_t mode) {
    if (running()) return EBUSY;
    if (!samples || !channels || !count || count % channels ||
        !std::isfinite(point_rate) || point_rate <= 0 || mode > AIO_DAC_REFILL_IRQ)
        return EINVAL;
    DacCapabilities caps{};
    int error = hw_.dac_capabilities(caps);
    if (error) return error;
    if (channels > caps.channels) return EINVAL;
    if (!std::isfinite(caps.max_sample_rate) || caps.max_sample_rate <= 0 ||
        caps.fifo_samples < channels || caps.fifo_samples > 1048576) return EPROTO;
    if (point_rate > caps.max_sample_rate / channels) return ERANGE;
    if (worker_.joinable()) worker_.join();
    samples_.assign(samples, samples + count); // Caller may release its buffer after Start.
    channels_ = channels;
    fifo_samples_ = caps.fifo_samples;
    mode_ = mode;
    position_ = 0;
    upload_.resize(fifo_samples_ - fifo_samples_ % channels);
    stopping_.store(false);
    checking_.store(false);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = {};
        status_.StructSize = sizeof(status_);
        status_.State = AIO_STATE_RUNNING;
        status_.ChannelCount = channels;
        status_.RefillMode = mode;
        status_.FIFOSamples = caps.fifo_samples;
        done_ = false;
        started_ = Clock::now();
    }
    try {
        double actual = 0;
        error = hw_.dac_prepare(channels, point_rate, mode == AIO_DAC_REFILL_IRQ, actual);
        if (!error && (!std::isfinite(actual) || actual <= 0 ||
                       actual > caps.max_sample_rate / channels)) error = EPROTO;
        if (!error) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                status_.ActualPointRateHz = actual;
            }
            // Poll eight times per FIFO period, with a bounded sleep. This is
            // host refill scheduling; the FPGA divisor clocks the DAC points.
            double micros = 1e6 * upload_.size() / (actual * channels * 8);
            poll_interval_ = std::chrono::microseconds(static_cast<int64_t>(
                std::max(1.0, std::min(1000.0, micros))));
            error = fill(static_cast<uint32_t>(upload_.size()));
        }
        if (!error) error = hw_.dac_start();
        if (!error) worker_ = std::thread([this]{ run(); });
    } catch (const std::bad_alloc &) { error = ENOMEM; }
      catch (const std::system_error &e) { error = e.code().value() > 0 ? e.code().value() : EIO; }
      catch (...) { error = EIO; }
    if (error) finish(error);
    return error;
}

void DacWaveform::finish(int error) noexcept {
    try {
        const int stop_error = hw_.dac_stop();
        if (!error) error = stop_error;
    } catch (...) { if (!error) error = EIO; }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.ErrorCode = error;
        status_.State = error ? AIO_STATE_FAILED : AIO_STATE_COMPLETE;
        status_.ElapsedNanoseconds = elapsed();
        done_ = true;
    }
    changed_.notify_all();
}

void DacWaveform::run() noexcept {
    int error = 0;
    try {
        while (!stopping_.load()) {
            uint32_t queued = 0;
            bool empty = false;
            error = hw_.dac_state(queued, empty);
            if (error) break;
            if (queued > fifo_samples_) { error = EPROTO; break; }
            if (empty || !queued) { error = EPIPE; break; }
            if (stopping_.load()) break;
            error = fill(fifo_samples_ - queued);
            if (error) break;
            if (mode_ == AIO_DAC_REFILL_IRQ) {
                if (stopping_.load()) break;
                error = hw_.wait_irq();
                if (stopping_.load()) {
                    if (error == ECANCELED || error == EINTR || error == EALREADY) error = 0;
                    break;
                }
                if (error == ECANCELED && checking_.exchange(false)) { error = 0; continue; }
                if (error == EINTR) { error = 0; continue; }
                if (error) break;
                checking_.store(false);
                std::lock_guard<std::mutex> lock(mutex_);
                ++status_.IRQWakeups;
            } else {
                std::unique_lock<std::mutex> lock(mutex_);
                changed_.wait_for(lock, poll_interval_, [&]{ return stopping_.load(); });
            }
        }
    } catch (const std::bad_alloc &) { error = ENOMEM; }
      catch (...) { error = EIO; }
    // Detect underruns even when no IRQ arrived and the caller stopped without
    // polling status. Check before dac_stop clears the physical FIFO.
    if (!error) {
        try {
            uint32_t queued = 0;
            bool empty = false;
            error = hw_.dac_state(queued, empty);
            if (!error && queued > fifo_samples_) error = EPROTO;
            if (!error && (empty || !queued)) error = EPIPE;
        } catch (...) { error = EIO; }
    }
    finish(error);
}

int DacWaveform::stop() noexcept {
    stopping_.store(true);
    changed_.notify_all();
    for (;;) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (done_) break;
        lock.unlock();
        // APCI cancel is not latched before a wait begins; repeat until the
        // worker acknowledges completion, closing the check-before-wait race.
        if (mode_ == AIO_DAC_REFILL_IRQ) {
            try { (void)hw_.cancel_irq(); } catch (...) {}
        }
        lock.lock();
        changed_.wait_for(lock, std::chrono::milliseconds(1), [&]{ return done_; });
    }
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.ErrorCode;
}
} // namespace aioaio
