#include "AIOAIO.h"
#include "fake_backend.hpp"

#include <chrono>
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
    REQUIRE(ADC_ConfigureAcquire(board.index, ctRate, 100) == ENOTSUP);
    REQUIRE(ADC_SetCallback(board.index, ctRaw, nullptr) == ENOTSUP);
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
        REQUIRE(AIO_Shutdown() == 0);
        std::cout << "Engine: finite/wrap/partial, conversion, cancellation, overflow, error, and restart tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        AIO_Shutdown();
        return 1;
    }
}
