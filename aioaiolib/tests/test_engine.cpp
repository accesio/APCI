#include "AIOAIO.h"
#include "fake_backend.hpp"

#include <chrono>
#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

#define REQUIRE(expr) do { if (!(expr)) throw std::runtime_error( \
    std::string(__func__) + ":" + std::to_string(__LINE__) + ": " #expr); } while (0)

namespace {
TADCAcquisitionStatus status_of(TBoardIndex board) {
    TADCAcquisitionStatus status{};
    status.StructSize = sizeof(status);
    REQUIRE(ADC_GetAcquisitionStatus(board, &status) == 0);
    return status;
}

TADCAcquisitionStatus wait_terminal(TBoardIndex board) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (;;) {
        const auto status = status_of(board);
        if (status.State != AIO_STATE_RUNNING) return status;
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

struct Board {
    TBoardIndex index = biNone;
    explicit Board(const char *path = "fake:paired") {
        fake::reset();
        REQUIRE(BRD_Open(path, &index) == 0);
        REQUIRE(index != biNone);
    }
    ~Board() { if (index != biNone) BRD_Close(index); }
};

void test_discovery_and_arguments() {
    REQUIRE(fake::open_count() == 0);
    REQUIRE(BRD_GetHighestBoardIndex() == biNone);
    REQUIRE(fake::open_count() == 0);
    REQUIRE(BRD_Open(nullptr, nullptr) != 0);
    TBoardIndex unsupported = biNone;
    REQUIRE(BRD_Open("fake:unsupported", &unsupported) == ENOTSUP);
    Board board;
    TBoardCapabilities caps{};
    caps.StructSize = sizeof(caps);
    REQUIRE(BRD_GetCapabilities(board.index, &caps) == 0);
    REQUIRE(caps.ADCCount == 2 && caps.MaxChannels == 16);
    REQUIRE(caps.FIFORowBytes == 8 && caps.ResolutionBits == 16);
    REQUIRE(caps.MaxADCRateHz == 1000000);
    // An application compiled against the original 384-byte capability
    // layout must not have the appended field written beyond its buffer.
    TBoardCapabilities legacy;
    std::memset(&legacy, 0xa5, sizeof(legacy));
    constexpr size_t legacy_size = offsetof(TBoardCapabilities, MaxADCRateHz);
    static_assert(legacy_size == 384, "original capability ABI");
    legacy.StructSize = legacy_size;
    REQUIRE(BRD_GetCapabilities(board.index, &legacy) == 0);
    REQUIRE(legacy.StructSize == legacy_size && legacy.ADCCount == 2);
    const auto *bytes = reinterpret_cast<const unsigned char *>(&legacy);
    for (size_t i = legacy_size; i < sizeof(legacy); ++i) REQUIRE(bytes[i] == 0xa5);
    // The later 392-byte rate-capability ABI must retain its rate field while
    // receiving no writes into the newly appended DAC capability fields.
    std::memset(&legacy, 0xa5, sizeof(legacy));
    constexpr size_t rate_size = offsetof(TBoardCapabilities, DACCount);
    static_assert(rate_size == 392, "rate capability ABI");
    legacy.StructSize = rate_size;
    REQUIRE(BRD_GetCapabilities(board.index, &legacy) == 0);
    REQUIRE(legacy.StructSize == rate_size && legacy.MaxADCRateHz == 1000000);
    for (size_t i = rate_size; i < sizeof(legacy); ++i) REQUIRE(bytes[i] == 0xa5);
    struct FutureCapabilities {
        TBoardCapabilities current;
        uint64_t future[2];
    } future;
    std::memset(&future, 0xa5, sizeof(future));
    future.current.StructSize = sizeof(future);
    REQUIRE(BRD_GetCapabilities(board.index, &future.current) == 0);
    REQUIRE(future.current.StructSize == sizeof(TBoardCapabilities));
    REQUIRE(future.current.MaxADCRateHz == 1000000);
    for (const auto field : future.future) REQUIRE(field == UINT64_C(0xa5a5a5a5a5a5a5a5));
    REQUIRE(BRD_GetCapabilities(board.index, nullptr) != 0);
    caps.StructSize = 0;
    REQUIRE(BRD_GetCapabilities(board.index, &caps) != 0);
    REQUIRE(ADC_SetRate(board.index, 0) != 0);
    REQUIRE(ADC_SetRate(board.index, std::numeric_limits<double>::infinity()) != 0);
    REQUIRE(ADC_SetRate(board.index, std::numeric_limits<double>::quiet_NaN()) != 0);
    REQUIRE(ADC_SetRate(board.index, 1000000) == 0);
    REQUIRE(ADC_SetRate(board.index, 1000001) == ERANGE);
    REQUIRE(fake::last_config().rate_hz == 1000000);
    REQUIRE(ADC_SetChannelCount(board.index, 3) != 0);
    REQUIRE(ADC_SetChannelCount(board.index, 18) != 0);
    REQUIRE(ADC_SetRange1(board.index, 16, rcBipolar10) != 0);
    REQUIRE(ADC_SetRate(biNone, 1000) != 0);
    REQUIRE(ADC_ConfigureAcquire(board.index, ctRate, 100) == ERANGE);
    REQUIRE(ADC_SetCallback(board.index, ctRaw, nullptr) == EINVAL);
    REQUIRE(DAC_OutputV(board.index, 0, 0) == ENOTSUP);
}

void test_model_rate_limit() {
    Board board("fake:slow");
    TBoardCapabilities caps{};
    caps.StructSize = sizeof(caps);
    REQUIRE(BRD_GetCapabilities(board.index, &caps) == 0);
    REQUIRE(caps.DeviceID == 0x805e && caps.ADCCount == 1);
    REQUIRE(caps.MaxADCRateHz == 100000);
    REQUIRE(ADC_SetRate(board.index, 100000) == 0);
    for (double hz : {100000.001, 250000.0, 1000000.0}) {
        REQUIRE(ADC_SetRate(board.index, hz) == ERANGE);
        REQUIRE(fake::last_config().rate_hz == 100000);
    }
    double actual = 0;
    REQUIRE(ADC_GetRate(board.index, &actual) == 0 && actual == 100000);
    REQUIRE(fake::start_count() == 0);
    REQUIRE(status_of(board.index).State == AIO_STATE_IDLE);
}

void test_finite_wrap_partial_and_restart() {
    Board board;
    REQUIRE(ADC_SetRate(board.index, 12345) == 0);
    REQUIRE(ADC_SetChannelCount(board.index, 16) == 0);
    REQUIRE(ADC_ConfigureAcquire(board.index, ctSize, 8) == 0);
    REQUIRE(ADC_AcquireBlock(board.index, 13) == 0);
    REQUIRE(fake::wait_started());
    REQUIRE(fake::words_per_slot() == 8);
    REQUIRE(ADC_SetRate(board.index, 1000) == EBUSY);
    REQUIRE(DAC_SetRange1(board.index, 0, rcBipolar10) == EBUSY);
    uint32_t dac_range = rcBipolar10;
    REQUIRE(DAC_SetRangeAll(board.index, 1, &dac_range) == EBUSY);
    REQUIRE(DAC_OutputCounts(board.index, 0, 0) == EBUSY);
    REQUIRE(DAC_OutputV(board.index, 0, 0) == EBUSY);
    uint32_t got = 99, raw[32]{};
    REQUIRE(ADC_GetDataRaw(board.index, 1, &got, raw) == EAGAIN && got == 0);
    REQUIRE(ADC_WaitData(board.index, 1) == ETIMEDOUT);
    fake::emit_slots(2);
    REQUIRE(fake::wait_acknowledged(2));
    auto status = wait_terminal(board.index);
    REQUIRE(status.State == AIO_STATE_COMPLETE && status.ErrorCode == 0);
    REQUIRE(status.RequestedSamples == 13 && status.CapturedSamples == 13);
    REQUIRE(status.QueuedSamples == 13 && status.DMACompletions == 2);
    REQUIRE(status.DMABytes == 64);
    REQUIRE(ADC_AcquireBlock(board.index, 1) == EBUSY); // preserve undrained results
    REQUIRE(ADC_WaitData(board.index, 0) == 0);
    REQUIRE(ADC_GetDataRaw(board.index, 3, &got, raw) == 0 && got == 3);
    for (uint32_t i = 0; i < got; ++i) REQUIRE(raw[i] == fake::sample_word(i));
    REQUIRE(ADC_GetDataRaw(board.index, 32, &got, raw) == 0 && got == 10);
    for (uint32_t i = 0; i < got; ++i) REQUIRE(raw[i] == fake::sample_word(i + 3));
    REQUIRE(ADC_GetDataRaw(board.index, 1, &got, raw) == ENODATA && got == 0);
    status = status_of(board.index);
    REQUIRE(status.DeliveredSamples == 13 && status.QueuedSamples == 0);
    REQUIRE(status.ActualRateHz == 12345);
    REQUIRE(fake::last_config().channels_per_adc == 8);

    REQUIRE(ADC_AcquireBlock(board.index, 3) == 0);
    REQUIRE(fake::wait_started());
    REQUIRE(fake::words_per_slot() == 4);
    fake::emit_slots(1, 100);
    REQUIRE(wait_terminal(board.index).State == AIO_STATE_COMPLETE);
    REQUIRE(ADC_GetDataRaw(board.index, 32, &got, raw) == 0 && got == 3);
    for (uint32_t i = 0; i < got; ++i) REQUIRE(raw[i] == fake::sample_word(i + 100));
    REQUIRE(status_of(board.index).CapturedSamples == 3);
}

void test_invalid_words_and_counts() {
    Board board;
    REQUIRE(ADC_ConfigureAcquire(board.index, ctSize, 4) == 0);
    REQUIRE(ADC_AcquireBlock(board.index, 3) == 0);
    REQUIRE(fake::wait_started());
    fake::emit_words({fake::sample_word(0xfffe), 0x80000000u, fake::sample_word(0xffff)});
    REQUIRE(fake::wait_acknowledged(1));
    fake::emit_words({fake::sample_word(0x10000)});
    REQUIRE(wait_terminal(board.index).State == AIO_STATE_COMPLETE);
    uint32_t got = 0;
    uint16_t counts[3]{}, statuses[3]{}, timestamps[3]{1, 1, 1};
    REQUIRE(ADC_GetDataCounts(board.index, 3, &got, counts, statuses, timestamps) == 0);
    REQUIRE(got == 3 && counts[0] == 0xfffe && counts[1] == 0xffff && counts[2] == 0);
    for (uint32_t i = 0; i < 3; ++i) {
        REQUIRE(statuses[i] == (fake::sample_word(0xfffe + i) >> 16));
        REQUIRE(timestamps[i] == 0);
    }
}

void test_voltage_conversion() {
    Board board;
    REQUIRE(ADC_AcquireBlock(board.index, 2) == 0);
    REQUIRE(fake::wait_started());
    fake::emit_words({(7u << 16) | 0x8000u, (7u << 16) | 0x4000u});
    REQUIRE(wait_terminal(board.index).State == AIO_STATE_COMPLETE);
    uint32_t got = 0;
    double volts[2]{};
    REQUIRE(ADC_GetDataV(board.index, 2, &got, volts, nullptr, nullptr) == 0 && got == 2);
    REQUIRE(std::abs(volts[0] - -20.48) < 1e-10);
    REQUIRE(std::abs(volts[1] - 10.24) < 1e-10);
}

void test_cancel_and_clear() {
    Board board;
    REQUIRE(ADC_ConfigureAcquire(board.index, ctSize, 8) == 0);
    REQUIRE(ADC_AcquireStream(board.index) == 0);
    REQUIRE(fake::wait_started());
    fake::emit_slots(1);
    REQUIRE(fake::wait_acknowledged(1));
    REQUIRE(ADC_ClearData(board.index) == EBUSY);
    uint32_t drained = 0, words[8]{};
    REQUIRE(ADC_GetDataRaw(board.index, 8, &drained, words) == 0 && drained == 8);
    REQUIRE(status_of(board.index).QueuedSamples == 0);
    // Allow the worker to enter its blocked IRQ wait, then discard one cancel
    // to reproduce the driver's unlatched cancellation window.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    fake::ignore_cancels(1);
    REQUIRE(ADC_AcquireEnd(board.index) == 0);
    REQUIRE(fake::cancel_count() >= 2);
    const auto status = status_of(board.index);
    REQUIRE(status.State == AIO_STATE_CANCELLED);
    uint32_t got = 99, raw = 0;
    REQUIRE(ADC_GetDataRaw(board.index, 1, &got, &raw) == ECANCELED && got == 0);
    REQUIRE(ADC_WaitData(board.index, 0) == ECANCELED);
    REQUIRE(ADC_ClearData(board.index) == 0);
    REQUIRE(fake::stop_count() != 0);
}

void test_driver_discard_and_error() {
    {
        Board board;
        REQUIRE(ADC_AcquireStream(board.index) == 0);
        REQUIRE(fake::wait_started());
        fake::discard_slots(3);
        const auto status = wait_terminal(board.index);
        REQUIRE(status.State == AIO_STATE_FAILED && status.ErrorCode == EOVERFLOW);
        REQUIRE(status.DiscardedDMASlots == 3);
        REQUIRE(ADC_WaitData(board.index, 0) == EOVERFLOW);
        REQUIRE(status_of(board.index).ErrorCode == EOVERFLOW);
    }
    {
        Board board;
        REQUIRE(ADC_AcquireStream(board.index) == 0);
        REQUIRE(fake::wait_started());
        fake::fail_ready(EIO);
        const auto status = wait_terminal(board.index);
        REQUIRE(status.State == AIO_STATE_FAILED && status.ErrorCode == EIO);
        uint32_t got = 99, raw = 0;
        REQUIRE(ADC_GetDataRaw(board.index, 1, &got, &raw) == EIO && got == 0);
        REQUIRE(status_of(board.index).ErrorCode == EIO);
    }
}

void test_slow_consumer_overflow() {
    Board board;
    REQUIRE(ADC_ConfigureAcquire(board.index, ctSize, 512) == 0);
    REQUIRE(ADC_AcquireStream(board.index) == 0);
    REQUIRE(fake::wait_started());
    // Fill the bounded application queue without consuming. Each batch is
    // acknowledged before reusing a fake DMA slot, so this is not driver loss.
    for (uint32_t i = 0; i < 17; ++i) {
        fake::emit_slots(1, i * 512);
        REQUIRE(fake::wait_acknowledged(i + 1));
    }
    auto status = wait_terminal(board.index);
    REQUIRE(status.State == AIO_STATE_FAILED && status.ErrorCode == ENOBUFS);
    REQUIRE(status.DiscardedDMASlots == 0);
    REQUIRE(status.CapturedSamples == 8192 && status.QueuedSamples == 8192);
    std::vector<uint32_t> raw(8192);
    uint32_t got = 0;
    REQUIRE(ADC_GetDataRaw(board.index, raw.size(), &got, raw.data()) == 0 && got == raw.size());
    for (uint32_t i = 0; i < got; ++i) REQUIRE(raw[i] == fake::sample_word(i));
    REQUIRE(ADC_GetDataRaw(board.index, 1, &got, raw.data()) == ENOBUFS && got == 0);
    REQUIRE(ADC_AcquireEnd(board.index) == ENOBUFS);
    REQUIRE(status_of(board.index).ErrorCode == ENOBUFS);
}

void test_throwing_startup_and_rollback() {
    Board board;
    fake::throw_on_start_and_stop(true, true);
    REQUIRE(ADC_AcquireBlock(board.index, 13) == EIO);
    const auto status = status_of(board.index);
    REQUIRE(status.State == AIO_STATE_FAILED && status.ErrorCode == EIO);
    REQUIRE(ADC_WaitData(board.index, 0) == EIO);
    REQUIRE(ADC_AcquireEnd(board.index) == EIO);
    REQUIRE(BRD_Close(board.index) == EIO);
    board.index = biNone;
    REQUIRE(fake::start_count() == 1 && fake::stop_count() == 1);
}

void test_immediate_and_registers() {
    Board board;
    uint32_t raw[16]{};
    double volts[16]{};
    REQUIRE(ADC_GetImmediateScanRaw(board.index, nullptr, rcNoChange) == EINVAL);
    REQUIRE(ADC_GetImmediateRaw(board.index, raw, ciAll, rcNoChange) == EINVAL);
    REQUIRE(ADC_GetImmediateScanRaw(board.index, raw, rcNoChange) == 0);
    for (uint32_t ch = 0; ch < 16; ++ch) REQUIRE((raw[ch] >> 20) == ch);
    REQUIRE(ADC_GetImmediateScanV(board.index, volts, rcBipolar5) == 0);
    REQUIRE(std::abs(volts[0] + 10.24) < 1e-10 && std::abs(volts[8] - 5.12) < 1e-10);
    REQUIRE(fake::last_immediate_config().range_codes[15] == 1);
    REQUIRE(ADC_GetImmediateV(board.index, volts, 8, rcNoChange) == 0);
    REQUIRE(std::abs(volts[0] - 10.24) < 1e-10); // Override did not persist.
    REQUIRE(ADC_SetDifferential(board.index, 1) == 0);
    REQUIRE(ADC_GetImmediateScanRaw(board.index, raw, rcNoChange) == 0);
    REQUIRE(ADC_GetImmediateScanV(board.index, volts, rcNoChange) == 0);
    for (uint32_t adc = 0; adc < 2; ++adc) {
        for (uint32_t ch = 0; ch < 4; ++ch)
            REQUIRE((raw[adc * 8 + ch] >> 20) == adc * 8 + ch * 2);
        for (uint32_t ch = 4; ch < 8; ++ch) {
            REQUIRE(raw[adc * 8 + ch] == 0x80000000u);
            REQUIRE(std::isnan(volts[adc * 8 + ch]));
        }
    }
    REQUIRE(ADC_GetImmediateRaw(board.index, raw, 8, rcNoChange) == 0 && (raw[0] >> 20) == 8);
    REQUIRE(ADC_GetImmediateRaw(board.index, raw, 4, rcNoChange) == EINVAL);
    REQUIRE(ADC_GetImmediateRaw(board.index, raw, 16, rcNoChange) == EINVAL);
    REQUIRE(ADC_GetImmediateRaw(board.index, raw, 0, 6) == EINVAL);

    // Range APIs retain physical input indexing. Immediate differential calls
    // select logical pairs, so pair 1 must use the configured range at input 2.
    REQUIRE(ADC_SetRange1(board.index, 1, rcBipolar5) == 0);
    REQUIRE(ADC_SetRange1(board.index, 2, rcBipolar2_5) == 0);
    REQUIRE(ADC_SetRange1(board.index, 10, rcBipolar1_25) == 0);
    REQUIRE(ADC_GetImmediateRaw(board.index, raw, 1, rcNoChange) == 0);
    REQUIRE(((raw[0] >> 16) & 7u) == 2);
    const auto configured = fake::last_immediate_config();
    REQUIRE(configured.range_codes[1] == 1 && configured.range_codes[2] == 2);
    REQUIRE(ADC_GetImmediateRaw(board.index, raw, 9, rcNoChange) == 0);
    REQUIRE(((raw[0] >> 16) & 7u) == 3);
    REQUIRE(ADC_GetImmediateRaw(board.index, raw, 1, rcBipolar10) == 0);
    REQUIRE(((raw[0] >> 16) & 7u) == 7);
    REQUIRE(ADC_GetImmediateRaw(board.index, raw, 1, rcNoChange) == 0);
    REQUIRE(fake::last_immediate_config().range_codes == configured.range_codes);
    REQUIRE(((raw[0] >> 16) & 7u) == 2);

    REQUIRE(PIO_Out8(board.index, 0x44, 0x12) == 0);
    errno = EIO; REQUIRE(PIO_In8(board.index, 0x44) == 0x12 && errno == 0);
    REQUIRE(PIO_Out16(board.index, 0x44, 0x3456) == 0);
    REQUIRE(PIO_In16(board.index, 0x44) == 0x3456 && errno == 0);
    REQUIRE(PIO_Out32(board.index, 0x44, 0x789abcde) == 0);
    REQUIRE(PIO_In32(board.index, 0x44) == 0x789abcde && errno == 0);
    REQUIRE(PIO_In16(board.index, 0x45) == 0 && errno == EINVAL);
    REQUIRE(PIO_In32(biNone, 0x44) == 0 && errno == ENODEV);
    uint8_t values8[]{1, 2, 3}, read8[3]{};
    uint16_t values16[]{0x1122, 0x3344, 0x5566}, read16[3]{};
    uint32_t values32[]{0x12345678, 0xabcdef01, 0x87654321}, read32[3]{};
    REQUIRE(PIO_OUTS8(board.index, 0x44, 3, values8) == 0);
    REQUIRE(PIO_INS8(board.index, 0x44, 3, read8) == 0);
    REQUIRE(PIO_OUTS16(board.index, 0x44, 3, values16) == 0);
    REQUIRE(PIO_INS16(board.index, 0x44, 3, read16) == 0);
    REQUIRE(PIO_OUTS32(board.index, 0x44, 3, values32) == 0);
    REQUIRE(PIO_INS32(board.index, 0x44, 3, read32) == 0);
    for (uint32_t i = 0; i < 3; ++i) REQUIRE(read8[i] == 3 && read16[i] == 0x5566 && read32[i] == 0x87654321);
    for (const auto &op : fake::register_operations()) REQUIRE(op.offset == 0x44);
    REQUIRE(PIO_INS32(board.index, 0x44, 1, nullptr) == EINVAL);
    REQUIRE(PIO_OUTS32(board.index, 0x44, 1, nullptr) == EINVAL);
    REQUIRE(PIO_INS8(board.index, 0x44, 0, nullptr) == 0);
    REQUIRE(PIO_OUTS8(board.index, 0x44, 0, nullptr) == 0);
    REQUIRE(ADC_AcquireStream(board.index) == 0);
    REQUIRE(ADC_GetImmediateScanRaw(board.index, raw, rcNoChange) == EBUSY);
    REQUIRE(PIO_In8(board.index, 0x44) == 0 && errno == EBUSY);
    REQUIRE(PIO_Out32(board.index, 0x44, 0) == EBUSY);
    REQUIRE(PIO_INS16(board.index, 0x44, 3, read16) == EBUSY);
    REQUIRE(PIO_OUTS16(board.index, 0x44, 3, values16) == EBUSY);
    REQUIRE(ADC_AcquireEnd(board.index) == 0);
}

void test_chunk_rate_and_reset() {
    Board board;
    REQUIRE(ADC_SetRate(board.index, 10000) == 0);
    REQUIRE(ADC_ConfigureAcquire(board.index, ctRate, 0) == EINVAL);
    REQUIRE(ADC_ConfigureAcquire(board.index, 999, 1) == EINVAL);
    REQUIRE(ADC_ConfigureAcquire(board.index, ctRate, 1) == ERANGE);
    REQUIRE(ADC_ConfigureAcquire(board.index, ctRate, 100) == 0);
    REQUIRE(ADC_AcquireStream(board.index) == 0);
    REQUIRE(fake::words_per_slot() == 200); // 20,000 scalar results / 100 batches.
    REQUIRE(ADC_AcquireEnd(board.index) == 0);
    REQUIRE(ADC_SetRate(board.index, 12345) == 0);
    REQUIRE(ADC_AcquireStream(board.index) == 0);
    REQUIRE(fake::words_per_slot() == 248); // Recomputed and rounded up to rows.
    REQUIRE(ADC_AcquireEnd(board.index) == 0);
    REQUIRE(ADC_ConfigureAcquire(board.index, ctRate, UINT32_MAX) == 0);
    REQUIRE(ADC_AcquireBlock(board.index, 1) == 0);
    REQUIRE(fake::words_per_slot() == 2);
    fake::emit_slots(1);
    REQUIRE(wait_terminal(board.index).CapturedSamples == 1);
    REQUIRE(BRD_Reset(board.index) == 0); // Clear undrained samples as well.
    REQUIRE(fake::reset_count() == 1);
    auto status = status_of(board.index);
    REQUIRE(status.State == AIO_STATE_IDLE && status.CapturedSamples == 0 && status.QueuedSamples == 0);
    double rate = 0;
    REQUIRE(ADC_GetRate(board.index, &rate) == 0 && rate == 10000);
    REQUIRE(!fake::last_config().differential && fake::last_config().channels_per_adc == 8);
    REQUIRE(ADC_AcquireStream(board.index) == 0);
    REQUIRE(fake::words_per_slot() == 2048);
    REQUIRE(BRD_Reset(board.index) == 0); // Stop and join active acquisition.
    REQUIRE(status_of(board.index).State == AIO_STATE_IDLE);
    REQUIRE(ADC_AcquireStream(board.index) == 0);
    fake::fail_ready(EIO);
    REQUIRE(wait_terminal(board.index).ErrorCode == EIO);
    REQUIRE(BRD_Reset(board.index) == 0); // A successful reset clears the earlier failure.
    REQUIRE(ADC_AcquireEnd(board.index) == 0);
    fake::fail_reset(EIO);
    REQUIRE(BRD_Reset(board.index) == EIO);
    REQUIRE(status_of(board.index).State == AIO_STATE_FAILED && status_of(board.index).ErrorCode == EIO);
    fake::fail_reset(0);
    REQUIRE(BRD_Reset(board.index) == 0 && ADC_AcquireEnd(board.index) == 0);
}

struct CallbackState {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<uint32_t> words;
    std::vector<uint16_t> counts, statuses, timestamps;
    std::vector<double> volts;
    std::vector<uint32_t> errors;
    uint32_t calls = 0;
    bool self_stop = false, throw_error = false, block = false, released = false;
};
CallbackState *callback_state = nullptr;

void callback_raw(TBoardIndex index, TUInt32 count, TPUInt32 words) {
    auto &state = *callback_state;
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        ++state.calls;
        state.words.insert(state.words.end(), words, words + count);
        state.cv.notify_all();
        if (state.block) state.cv.wait(lock, [&]{ return state.released; });
    }
    if (state.self_stop || state.block) {
        TADCAcquisitionStatus status{}; status.StructSize = sizeof(status);
        TBoardCapabilities caps{}; caps.StructSize = sizeof(caps);
        double rate = 0;
        uint32_t dummy = 0, got = 0;
        // No callback call may wait on the mutex held by an external stopper.
        const uint32_t errors[]{ADC_SetRate(index, 1234), ADC_GetRate(index, &rate),
            BRD_Reset(index), BRD_Close(index), AIO_Shutdown(),
            ADC_AcquireStream(index), ADC_ConfigureAcquire(index, ctSize, 2),
            ADC_SetCallback(index, ctNone, nullptr), PIO_Out32(index, 0x44, 0),
            ADC_ClearData(index), DAC_StopWaveform(index),
            ADC_GetImmediateRaw(index, &dummy, 0, rcNoChange)};
        std::lock_guard<std::mutex> lock(state.mutex);
        state.errors.insert(state.errors.end(), std::begin(errors), std::end(errors));
        state.errors.push_back(ADC_GetAcquisitionStatus(index, &status));
        state.errors.push_back(BRD_GetCapabilities(index, &caps));
        state.errors.push_back(ADC_GetDataRaw(index, 1, &got, &dummy));
        state.errors.push_back(ADC_WaitData(index, 0));
        state.errors.push_back(ADC_PollData(index));
        state.errors.push_back(ADC_AcquireEnd(index));
    }
    if (state.throw_error) throw std::runtime_error("intentional callback failure");
}
void callback_counts(TBoardIndex, TUInt32 count, TPUInt16 counts, TPUInt16 statuses, TPUInt16 timestamps) {
    auto &state = *callback_state;
    std::lock_guard<std::mutex> lock(state.mutex);
    ++state.calls;
    state.counts.insert(state.counts.end(), counts, counts + count);
    state.statuses.insert(state.statuses.end(), statuses, statuses + count);
    state.timestamps.insert(state.timestamps.end(), timestamps, timestamps + count);
    std::fill(timestamps, timestamps + count, 0xabcd); // Next delivery must still have zero timestamps.
    state.cv.notify_all();
}
void callback_volts(TBoardIndex, TUInt32 count, TPDouble volts, TPUInt16 statuses, TPUInt16 timestamps) {
    auto &state = *callback_state;
    std::lock_guard<std::mutex> lock(state.mutex);
    ++state.calls;
    state.volts.insert(state.volts.end(), volts, volts + count);
    state.statuses.insert(state.statuses.end(), statuses, statuses + count);
    state.timestamps.insert(state.timestamps.end(), timestamps, timestamps + count);
    state.cv.notify_all();
}
void wait_callback(CallbackState &state, uint32_t count) {
    std::unique_lock<std::mutex> lock(state.mutex);
    REQUIRE(state.cv.wait_for(lock, std::chrono::seconds(2), [&]{ return state.calls >= count; }));
}
void check_callback_reentry_errors(const CallbackState &state) {
    REQUIRE(state.errors.size() == 18);
    for (size_t i = 0; i < 12; ++i) REQUIRE(state.errors[i] == EDEADLK);
    REQUIRE(state.errors[12] == 0 && state.errors[13] == 0);
    REQUIRE(state.errors[14] == EBUSY && state.errors[15] == EBUSY && state.errors[16] == EBUSY);
    REQUIRE(state.errors[17] == 0);
}

void test_callbacks() {
    Board board;
    CallbackState state; callback_state = &state;
    REQUIRE(ADC_SetCallback(board.index, 999, reinterpret_cast<TPointer>(callback_raw)) == EINVAL);
    REQUIRE(ADC_SetCallback(board.index, ctNone, reinterpret_cast<TPointer>(callback_raw)) == EINVAL);
    REQUIRE(ADC_SetCallback(board.index, ctRaw, nullptr) == EINVAL);
    REQUIRE(ADC_SetCallback(board.index, ctRaw, reinterpret_cast<TPointer>(callback_raw)) == 0);
    REQUIRE(ADC_ConfigureAcquire(board.index, ctSize, 4) == 0);
    REQUIRE(ADC_AcquireBlock(board.index, 6) == 0);
    REQUIRE(ADC_SetCallback(board.index, ctNone, nullptr) == EBUSY);
    fake::emit_slots(2);
    auto status = wait_terminal(board.index);
    REQUIRE(status.State == AIO_STATE_COMPLETE && status.CapturedSamples == 6 && status.DeliveredSamples == 6);
    REQUIRE(status.QueuedSamples == 0 && state.calls == 2 && state.words.size() == 6);
    for (uint32_t i = 0; i < 6; ++i) REQUIRE(state.words[i] == fake::sample_word(i));
    REQUIRE(ADC_PollData(board.index) == EBUSY);
    REQUIRE(ADC_SetCallback(board.index, ctCounts, reinterpret_cast<TPointer>(callback_counts)) == 0);
    REQUIRE(ADC_ConfigureAcquire(board.index, ctSize, 2) == 0);
    REQUIRE(ADC_AcquireBlock(board.index, 4) == 0);
    fake::emit_words({0x00078000u, 0x0017ffffu});
    REQUIRE(fake::wait_acknowledged(3));
    fake::emit_words({0x00270001u, 0x00374000u});
    REQUIRE(wait_terminal(board.index).DeliveredSamples == 4);
    REQUIRE(state.counts == std::vector<uint16_t>({0x8000, 0xffff, 1, 0x4000}));
    REQUIRE(state.statuses == std::vector<uint16_t>({7, 0x17, 0x27, 0x37}));
    for (auto stamp : state.timestamps) REQUIRE(stamp == 0);
    REQUIRE(ADC_SetCallback(board.index, ctV, reinterpret_cast<TPointer>(callback_volts)) == 0);
    REQUIRE(ADC_AcquireBlock(board.index, 2) == 0);
    fake::emit_words({0x00078000u, 0x00174000u});
    REQUIRE(wait_terminal(board.index).DeliveredSamples == 2);
    REQUIRE(state.volts.size() == 2 && std::abs(state.volts[0] + 20.48) < 1e-10 && std::abs(state.volts[1] - 10.24) < 1e-10);
    REQUIRE(BRD_Reset(board.index) == 0); // Keep registration through reset.
    REQUIRE(ADC_AcquireBlock(board.index, 2) == 0);
    fake::emit_words({0x00070000u, 0x00170000u});
    REQUIRE(wait_terminal(board.index).DeliveredSamples == 2 && state.volts.size() == 4);
    REQUIRE(ADC_SetCallback(board.index, ctNone, nullptr) == 0);
    REQUIRE(ADC_AcquireBlock(board.index, 2) == 0);
    fake::emit_slots(1);
    REQUIRE(wait_terminal(board.index).QueuedSamples == 2);
    REQUIRE(ADC_SetCallback(board.index, ctRaw, reinterpret_cast<TPointer>(callback_raw)) == EBUSY);
    REQUIRE(ADC_ClearData(board.index) == 0);
}

void test_callback_stream_and_self_stop() {
    Board board;
    CallbackState state; callback_state = &state;
    REQUIRE(ADC_SetCallback(board.index, ctRaw, reinterpret_cast<TPointer>(callback_raw)) == 0);
    REQUIRE(ADC_ConfigureAcquire(board.index, ctSize, 512) == 0);
    REQUIRE(ADC_AcquireStream(board.index) == 0);
    for (uint32_t i = 0; i < 20; ++i) {
        fake::emit_slots(1, i * 512);
        wait_callback(state, i + 1);
    }
    REQUIRE(ADC_AcquireEnd(board.index) == 0);
    auto status = status_of(board.index);
    REQUIRE(status.CapturedSamples == 10240 && status.DeliveredSamples == 10240 && status.QueuedSamples == 0);
    REQUIRE(status.ErrorCode == 0); // Callback mode does not fill a hidden queue.
    state.self_stop = true;
    REQUIRE(ADC_AcquireStream(board.index) == 0);
    fake::emit_slots(1);
    REQUIRE(wait_terminal(board.index).State == AIO_STATE_CANCELLED);
    check_callback_reentry_errors(state);
    state.self_stop = false;
    state.throw_error = true;
    REQUIRE(ADC_AcquireBlock(board.index, 1) == 0);
    fake::emit_slots(1);
    REQUIRE(wait_terminal(board.index).ErrorCode == EIO);
    REQUIRE(BRD_Reset(board.index) == 0 && ADC_AcquireEnd(board.index) == 0);
}

void test_callback_external_stop_race() {
    Board board;
    CallbackState state; callback_state = &state; state.block = true;
    REQUIRE(ADC_SetCallback(board.index, ctRaw, reinterpret_cast<TPointer>(callback_raw)) == 0);
    REQUIRE(ADC_AcquireStream(board.index) == 0);
    fake::emit_slots(1);
    wait_callback(state, 1);
    auto stopper = std::async(std::launch::async, [&]{ return ADC_AcquireEnd(board.index); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (fake::cancel_count() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool stopper_waiting = fake::cancel_count() != 0;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.released = true;
        state.cv.notify_all();
    }
    REQUIRE(stopper.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    REQUIRE(stopper.get() == 0 && stopper_waiting);
    check_callback_reentry_errors(state);
    REQUIRE(status_of(board.index).State == AIO_STATE_CANCELLED);
    const uint32_t calls = state.calls;
    REQUIRE(BRD_Close(board.index) == 0);
    board.index = biNone;
    REQUIRE(state.calls == calls); // Close returned only after the final callback.
}
} // namespace

int main() {
    try {
        test_discovery_and_arguments();
        test_model_rate_limit();
        test_finite_wrap_partial_and_restart();
        test_invalid_words_and_counts();
        test_voltage_conversion();
        test_cancel_and_clear();
        test_driver_discard_and_error();
        test_slow_consumer_overflow();
        test_throwing_startup_and_rollback();
        test_immediate_and_registers();
        test_chunk_rate_and_reset();
        test_callbacks();
        test_callback_stream_and_self_stop();
        test_callback_external_stop_race();
        REQUIRE(AIO_Shutdown() == 0);
        std::cout << "Engine: DMA, immediate ADC, register I/O, reset, chunk rates, callbacks and reentrant cancellation passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        AIO_Shutdown();
        return 1;
    }
}
