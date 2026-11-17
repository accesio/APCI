#include "AIOAIO.h"
#include "backend.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#define REQUIRE(e) do { if (!(e)) throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + ": " #e); } while (0)
namespace {
const aioaio::Model model{0xc0eb, "Fake FDS", 2, 8, 16, 2, 2047, 1000000};
struct State {
    std::mutex mutex;
    std::condition_variable cv;
    uint32_t queued = 0, cancels = 0, ignore_cancels = 0, stops = 0, starts = 0, waits = 0;
    bool pending = false, cancelled = false, adc = false, unsupported = false, waiting = false;
    int prepare_error = 0, upload_error = 0, state_error = 0, stop_error = 0, wait_error = 0;
    bool throw_start = false;
    std::vector<uint32_t> uploaded;
} state;

class Device : public aioaio::Backend {
    std::string path_ = "fake:dac";
public:
    const aioaio::Model &model() const noexcept override { return ::model; }
    const std::string &path() const noexcept override { return path_; }
    int configure(const aioaio::Config &c, double &rate) override { rate = c.rate_hz; return 0; }
    int prepare_dma(uint32_t, uint32_t) override { return 0; }
    int reset_board() override {
        std::lock_guard<std::mutex> l(state.mutex);
        state.queued = 0;
        state.adc = state.pending = state.cancelled = false;
        state.prepare_error = state.upload_error = state.state_error = state.stop_error = state.wait_error = 0;
        return 0;
    }
    int start_adc() override { std::lock_guard<std::mutex> l(state.mutex); state.adc = true; state.cancelled = false; return 0; }
    int stop_adc() override { std::lock_guard<std::mutex> l(state.mutex); state.adc = false; return 0; }
    int ready(uint32_t &a, uint32_t &b, uint32_t &c) override { a = b = c = 0; return 0; }
    int acknowledge(uint32_t) override { return 0; }
    const uint32_t *slot(uint32_t) const noexcept override { return nullptr; }
    int wait_irq() override {
        std::unique_lock<std::mutex> l(state.mutex);
        ++state.waits; state.waiting = true; state.cv.notify_all();
        state.cv.wait(l, [] { return state.pending || state.cancelled || state.wait_error; });
        state.waiting = false;
        if (state.wait_error) return state.wait_error;
        if (state.pending) { state.pending = state.cancelled = false; return 0; } // IRQ wins cancellation.
        state.cancelled = false; return ECANCELED;
    }
    int cancel_irq() override {
        std::lock_guard<std::mutex> l(state.mutex);
        ++state.cancels;
        if (state.ignore_cancels) { --state.ignore_cancels; return EALREADY; }
        if (!state.waiting) return EALREADY;
        state.cancelled = true;
        state.cv.notify_all();
        return 0;
    }
    int dac_capabilities(aioaio::DacCapabilities &out) override {
        std::lock_guard<std::mutex> l(state.mutex);
        if (state.unsupported) return ENOTSUP;
        out = {4, 10, 0x41234, 1000000}; return 0;
    }
    int dac_prepare(uint32_t, double rate, bool, double &actual) override {
        std::lock_guard<std::mutex> l(state.mutex);
        state.queued = 0; state.cancelled = state.pending = false;
        state.uploaded.clear(); actual = rate;
        return state.prepare_error;
    }
    int dac_upload(const uint32_t *words, uint32_t count) override {
        std::lock_guard<std::mutex> l(state.mutex);
        if (state.upload_error) return state.upload_error;
        REQUIRE(count <= 10 - state.queued);
        state.uploaded.insert(state.uploaded.end(), words, words + count);
        state.queued += count;
        state.cv.notify_all(); return 0;
    }
    int dac_start() override {
        std::lock_guard<std::mutex> l(state.mutex);
        ++state.starts;
        if (state.throw_start) throw std::runtime_error("start exception");
        return 0;
    }
    int dac_state(uint32_t &queued, bool &empty) override {
        std::lock_guard<std::mutex> l(state.mutex);
        queued = state.queued; empty = !queued; return state.state_error;
    }
    int dac_stop() override {
        std::lock_guard<std::mutex> l(state.mutex);
        ++state.stops; state.queued = 0; return state.stop_error;
    }
};
TBoardIndex open_board() {
    REQUIRE(AIO_Shutdown() == 0);
    std::lock_guard<std::mutex> l(state.mutex);
    state.queued = state.cancels = state.ignore_cancels = state.stops = state.starts = state.waits = 0;
    state.pending = state.cancelled = state.adc = state.unsupported = state.throw_start = state.waiting = false;
    state.prepare_error = state.upload_error = state.state_error = state.stop_error = state.wait_error = 0;
    state.uploaded.clear();
    TBoardIndex board = biNone;
    REQUIRE(BRD_Open("fake:dac", &board) == 0);
    return board;
}
TDACWaveformStatus status_of(TBoardIndex board) {
    TDACWaveformStatus s{}; s.StructSize = sizeof(s);
    REQUIRE(DAC_GetWaveformStatus(board, &s) == 0); return s;
}
template<class F> void until(F condition) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!condition()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void consume(uint32_t n, bool irq = true) {
    std::lock_guard<std::mutex> l(state.mutex);
    REQUIRE(n <= state.queued);
    state.queued -= n; state.pending = irq; state.cv.notify_all();
}
const uint16_t values[]{1,2,3,4,5,6};
void wrap_and_stop(uint32_t mode) {
    auto board = open_board();
    uint16_t source[]{1,2,3,4,5,6};
    REQUIRE(DAC_StartWaveform(board, 3, 6, source, 10000, mode) == 0);
    std::fill(std::begin(source), std::end(source), 0); // Start owns its copy.
    REQUIRE(ADC_AcquireStream(board) == EBUSY);
    REQUIRE(ADC_SetRate(board, 10000) == EBUSY);
    REQUIRE(DAC_SetRange1(board, 0, rcBipolar10) == EBUSY);
    uint32_t range = rcBipolar10;
    REQUIRE(DAC_SetRangeAll(board, 1, &range) == EBUSY);
    REQUIRE(DAC_OutputCounts(board, 0, 0) == EBUSY);
    REQUIRE(DAC_OutputV(board, 0, 0) == EBUSY);
    REQUIRE(DAC_StartWaveform(board, 3, 6, values, 10000, mode) == EBUSY);
    consume(3);
    until([&]{ return status_of(board).UploadedSamples >= 12; });
    {
        std::lock_guard<std::mutex> l(state.mutex);
        REQUIRE(state.uploaded.size() == 12);
        for (size_t i = 0; i < state.uploaded.size(); ++i)
            REQUIRE(state.uploaded[i] == (uint32_t(i % 3) << 16 | values[i % 6]));
        state.ignore_cancels = mode == AIO_DAC_REFILL_IRQ ? 3 : 0;
    }
    if (mode == AIO_DAC_REFILL_IRQ) until([] {
        std::lock_guard<std::mutex> l(state.mutex);
        return state.waiting && !state.cancelled && !state.pending;
    });
    REQUIRE(DAC_StopWaveform(board) == 0);
    auto s = status_of(board);
    REQUIRE(s.State == AIO_STATE_COMPLETE && s.ErrorCode == 0 && s.ChannelCount == 3);
    REQUIRE(s.Refills == 2 && s.UploadedSamples == 12 && s.ElapsedNanoseconds > 0);
    {
        std::lock_guard<std::mutex> l(state.mutex);
        REQUIRE(state.stops == 1);
        if (mode == AIO_DAC_REFILL_IRQ) REQUIRE(state.cancels >= 4);
    }
    REQUIRE(DAC_StopWaveform(board) == 0); // Idempotent; no duplicate hardware stop.
    REQUIRE(DAC_StartWaveform(board, 3, 6, values, 10000, mode) == 0);
    REQUIRE(BRD_Close(board) == 0); // Close cancels/joins a live worker.
}
void validation() {
    auto board = open_board();
    TDACWaveformCapabilities caps{}; caps.StructSize = sizeof(caps);
    REQUIRE(DAC_GetWaveformCapabilities(board, &caps) == 0);
    REQUIRE(caps.ChannelCount == 4 && caps.FIFOSamples == 10 && caps.MaxSampleRateHz == 1000000);
    REQUIRE(DAC_StartWaveform(board, 0, 6, values, 10000, 0) == EINVAL);
    REQUIRE(DAC_StartWaveform(board, 5, 5, values, 10000, 0) == EINVAL);
    REQUIRE(DAC_StartWaveform(board, 3, 5, values, 10000, 0) == EINVAL);
    REQUIRE(DAC_StartWaveform(board, 3, 6, nullptr, 10000, 0) == EINVAL);
    REQUIRE(DAC_StartWaveform(board, 3, 6, values, 0, 0) == EINVAL);
    REQUIRE(DAC_StartWaveform(board, 3, 6, values, std::numeric_limits<double>::infinity(), 0) == EINVAL);
    REQUIRE(DAC_StartWaveform(board, 3, 6, values, 10000, 2) == EINVAL);
    REQUIRE(DAC_StartWaveform(board, 3, 6, values, 1000000, 0) == ERANGE);
    REQUIRE(DAC_GetWaveformStatus(board, nullptr) == EINVAL);
    {
        std::lock_guard<std::mutex> l(state.mutex);
        REQUIRE(state.starts == 0 && state.stops == 0 && state.uploaded.empty());
        state.unsupported = true;
    }
    REQUIRE(DAC_GetWaveformCapabilities(board, &caps) == ENOTSUP);
    REQUIRE(DAC_StartWaveform(board, 3, 6, values, 10000, 0) == ENOTSUP);
    REQUIRE(BRD_Close(board) == 0);
    REQUIRE(DAC_StopWaveform(board) == ENODEV);
}
void errors() {
    for (int scenario = 0; scenario < 6; ++scenario) {
        auto board = open_board();
        int expected = EIO;
        {
            std::lock_guard<std::mutex> l(state.mutex);
            if (scenario == 0) state.prepare_error = EIO;
            if (scenario == 1) state.upload_error = EIO;
            if (scenario == 2) state.throw_start = true;
        }
        int error = DAC_StartWaveform(board, 3, 6, values, 10000, AIO_DAC_REFILL_IRQ);
        if (scenario < 3) REQUIRE(error == EIO);
        else {
            REQUIRE(error == 0);
            if (scenario == 3) { consume(9); expected = EPIPE; }
            else {
                std::lock_guard<std::mutex> l(state.mutex);
                if (scenario == 4) state.state_error = EIO;
                else { state.queued = 11; expected = EPROTO; }
                state.pending = true; state.cv.notify_all();
            }
        }
        until([&]{ return status_of(board).State == AIO_STATE_FAILED; });
        REQUIRE(status_of(board).ErrorCode == uint32_t(expected));
        REQUIRE(DAC_StopWaveform(board) == uint32_t(expected));
        REQUIRE(BRD_Close(board) == uint32_t(expected));
    }
    auto board = open_board();
    REQUIRE(DAC_StartWaveform(board, 3, 6, values, 10000, 0) == 0);
    { std::lock_guard<std::mutex> l(state.mutex); state.stop_error = EIO; }
    REQUIRE(DAC_StopWaveform(board) == EIO);
    REQUIRE(BRD_Close(board) == EIO);
}
void lost_irq_and_stop_error() {
    for (int scenario = 0; scenario < 3; ++scenario) {
        auto board = open_board();
        REQUIRE(DAC_StartWaveform(board, 3, 6, values, 10000, AIO_DAC_REFILL_IRQ) == 0);
        until([] { std::lock_guard<std::mutex> l(state.mutex); return state.waits > 0; });
        int expected = EPIPE;
        if (scenario < 2) consume(9, false); // Physical underrun with no IRQ notification.
        else {
            std::lock_guard<std::mutex> l(state.mutex);
            state.wait_error = EIO; expected = EIO;
        }
        if (scenario == 0) until([&]{ return status_of(board).State == AIO_STATE_FAILED; });
        REQUIRE(DAC_StopWaveform(board) == uint32_t(expected));
        REQUIRE(status_of(board).ErrorCode == uint32_t(expected));
        REQUIRE(BRD_Close(board) == uint32_t(expected));
    }
}
void reset_recovers_waveform() {
    auto board = open_board();
    REQUIRE(DAC_StartWaveform(board, 3, 6, values, 10000, AIO_DAC_REFILL_IRQ) == 0);
    {
        std::lock_guard<std::mutex> l(state.mutex);
        state.state_error = EIO;
        state.pending = true;
        state.cv.notify_all();
    }
    until([&]{ return status_of(board).State == AIO_STATE_FAILED; });
    REQUIRE(DAC_StopWaveform(board) == EIO);
    REQUIRE(BRD_Reset(board) == 0);
    auto status = status_of(board);
    REQUIRE(status.State == AIO_STATE_IDLE && status.ErrorCode == 0 && status.UploadedSamples == 0);
    REQUIRE(DAC_StopWaveform(board) == 0);
    REQUIRE(DAC_StartWaveform(board, 3, 6, values, 10000, AIO_DAC_REFILL_IRQ) == 0);
    uint32_t raw[16]{};
    REQUIRE(ADC_GetImmediateScanRaw(board, raw, rcNoChange) == EBUSY);
    REQUIRE(PIO_In32(board, 0x44) == 0 && errno == EBUSY);
    REQUIRE(PIO_Out32(board, 0x44, 0) == EBUSY);
    REQUIRE(PIO_INS32(board, 0x44, 1, raw) == EBUSY);
    REQUIRE(PIO_OUTS32(board, 0x44, 1, raw) == EBUSY);
    REQUIRE(BRD_Reset(board) == 0); // Stop and join a live waveform before reset.
    REQUIRE(status_of(board).State == AIO_STATE_IDLE);
    REQUIRE(AIO_Shutdown() == 0); // No stale playback error after successful reset.
}
void adc_ownership() {
    auto board = open_board();
    REQUIRE(ADC_AcquireStream(board) == 0);
    REQUIRE(DAC_StartWaveform(board, 3, 6, values, 10000, 0) == EBUSY);
    REQUIRE(ADC_AcquireEnd(board) == 0);
    REQUIRE(DAC_StartWaveform(board, 3, 6, values, 10000, 1) == 0);
    REQUIRE(AIO_Shutdown() == 0);
    REQUIRE(DAC_StopWaveform(board) == ENODEV);
}
}
namespace aioaio {
const Model *find_model(uint32_t) noexcept { return &::model; }
int open_backend(const std::string &, std::unique_ptr<Backend> &out) { out = std::make_unique<Device>(); return 0; }
}
int main() {
    try {
        validation(); wrap_and_stop(AIO_DAC_REFILL_POLL); wrap_and_stop(AIO_DAC_REFILL_IRQ);
        errors(); lost_irq_and_stop_error(); reset_recovers_waveform(); adc_ownership();
        REQUIRE(AIO_Shutdown() == 0);
        std::cout << "DAC engine: copied/interleaved wrap, polling/IRQ, cancellation race, errors, restart and ADC exclusion passed.\n";
    } catch (const std::exception &e) { (void)AIO_Shutdown(); std::cerr << e.what() << '\n'; return 1; }
}
