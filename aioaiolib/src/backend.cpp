#include "backend.hpp"
#include "AIOAIO.h"
#include "apcilib.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <new>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace aioaio {
namespace {

// APCI maps physical PCI BAR2 into logical region 1 for native AxIO.
constexpr int registers = 1;
constexpr uint32_t clock_offset = 0x0c;
constexpr uint32_t divisor_offset = 0x10;
constexpr uint32_t gains_offset = 0x18;
constexpr uint32_t threshold_offset = 0x20;
constexpr uint32_t fifo_count_offset = 0x28;
constexpr uint32_t control_offset = 0x38;
constexpr uint32_t irq_offset = 0x40;
constexpr uint32_t dio_control_offset = 0x48;
constexpr uint32_t go = 1u << 16;
constexpr uint32_t load_config = 1u << 17;
constexpr uint32_t adc_irq_mask = 0x003f;
constexpr uint32_t dma_irqs = (1u << 0) | (1u << 2);
constexpr uint32_t dac_command_offset = 0x04;
constexpr uint32_t dac_divisor_offset = 0x08;
constexpr uint32_t dac_fifo_offset = 0x50;
constexpr uint32_t dac_channels_offset = 0x54;
constexpr uint32_t dac_fifo_size_offset = 0x58;
constexpr uint32_t dac_irq = 1u << 9;
constexpr uint32_t dac_empty = 1u << 29;
constexpr uint32_t dac_spi_busy = 1u << 31;
constexpr uint32_t dac_external_modes = (1u << 20) | (1u << 27);
constexpr double dac_scalar_rate = 1000000.0;
constexpr size_t dac_staging_target = 64u * 1024u;

// IDs and chip counts come from CoreExports.pas and APCI apci_dev.h.
// Rate limits are per-ADC product ratings; see README for sources and the
// existing DAAI device-ID/firmware mapping caveat.
// Native ADIO register maps document an 11-bit FIFO threshold. Use the
// conservative limit even on firmware with larger physical FIFOs.
#define M(id, name, count, bits, max_rate, chip, dacs) \
    {id, name, count, 8, bits, 2, 2047, max_rate, DacChip::chip, dacs}
constexpr Model models[] = {
    M(0xc0e8, "mPCIe-AIO16-16F", 2, 16, 1000000, ltc2664, 4),
    M(0xc0e9, "mPCIe-AIO16-16A", 2, 16, 500000, ltc2664, 4),
    M(0xc0ea, "mPCIe-AIO16-16E", 2, 16, 250000, ltc2664, 4),
    M(0xc0eb, "mPCIe-AIO16-16FDS", 2, 16, 1000000, ltc2664, 4),
    M(0x80e8, "mPCIe-AI16-16F", 2, 16, 1000000, none, 0),
    M(0x80e9, "mPCIe-AI16-16A", 2, 16, 500000, none, 0),
    M(0x80ea, "mPCIe-AI16-16E", 2, 16, 250000, none, 0),
    M(0xc058, "mPCIe-AIO12-16A", 2, 12, 500000, ltc2664, 4),
    M(0xc059, "mPCIe-AIO12-16", 2, 12, 250000, ltc2664, 4),
    M(0xc05a, "mPCIe-AIO12-16E", 2, 12, 100000, ltc2664, 4),
    M(0x8058, "mPCIe-AI12-16A", 2, 12, 500000, none, 0),
    M(0x8059, "mPCIe-AI12-16", 2, 12, 250000, none, 0),
    M(0x805a, "mPCIe-AI12-16E", 2, 12, 100000, none, 0),
    M(0xc2ec, "PCIe-ADIO16-16F", 2, 16, 1000000, ltc2664, 4),
    M(0xc2ed, "PCIe-ADIO16-16A", 2, 16, 500000, ltc2664, 4),
    M(0xc2ee, "PCIe-ADIO16-16E", 2, 16, 250000, ltc2664, 4),
    M(0xc2ef, "PCIe-ADIO16-16FDS", 2, 16, 1000000, ltc2664, 4),
    M(0x82ec, "PCIe-ADI16-16F", 2, 16, 1000000, none, 0),
    M(0x82ed, "PCIe-ADI16-16A", 2, 16, 500000, none, 0),
    M(0x82ee, "PCIe-ADI16-16E", 2, 16, 250000, none, 0),
    M(0xc25c, "PCIe-ADIO12-16A", 2, 12, 500000, ltc2664, 4),
    M(0xc25d, "PCIe-ADIO12-16", 2, 12, 250000, ltc2664, 4),
    M(0xc25e, "PCIe-ADIO12-16E", 2, 12, 100000, ltc2664, 4),
    M(0x825c, "PCIe-ADI12-16A", 2, 12, 500000, none, 0),
    M(0x825d, "PCIe-ADI12-16", 2, 12, 250000, none, 0),
    M(0x825e, "PCIe-ADI12-16E", 2, 12, 100000, none, 0),
    M(0xc0ec, "mPCIe-ADIO16-8F", 1, 16, 1000000, ltc2664, 4),
    M(0xc0ed, "mPCIe-ADIO16-8A", 1, 16, 500000, ltc2664, 4),
    M(0xc0ee, "mPCIe-ADIO16-8E", 1, 16, 250000, ltc2664, 4),
    M(0xc0ef, "mPCIe-ADIO16-8FDS", 1, 16, 1000000, ltc2664, 4),
    M(0x80ec, "mPCIe-ADI16-8F", 1, 16, 1000000, none, 0),
    M(0x80ed, "mPCIe-ADI16-8A", 1, 16, 500000, none, 0),
    M(0x80ee, "mPCIe-ADI16-8E", 1, 16, 250000, none, 0),
    M(0xc05c, "mPCIe-ADIO12-8A", 1, 12, 500000, ltc2664, 4),
    M(0xc05d, "mPCIe-ADIO12-8", 1, 12, 250000, ltc2664, 4),
    M(0xc05e, "mPCIe-ADIO12-8E", 1, 12, 100000, ltc2664, 4),
    M(0x805c, "mPCIe-ADI12-8A", 1, 12, 500000, none, 0),
    M(0x805d, "mPCIe-ADI12-8", 1, 12, 250000, none, 0),
    M(0x805e, "mPCIe-ADI12-8E", 1, 12, 100000, none, 0),
    M(0xcee8, "mPCIe-DAAI16-8F", 1, 16, 1000000, ad5755, 8),
    M(0xcee9, "mPCIe-DAAI16-8A", 1, 16, 500000, ad5755, 8),
    M(0xceea, "mPCIe-DAAI16-8E", 1, 16, 250000, ad5755, 8),
    M(0xce50, "mPCIe-DAAI12-8", 1, 12, 250000, ad5755, 8),
    M(0xce51, "mPCIe-DAAI12-8E", 1, 12, 100000, ad5755, 8),
    M(0xce52, "mPCIe-DAAI12-8A", 1, 12, 500000, ad5755, 8),
    M(0xced8, "mPCIe-DAAI16-4F", 1, 16, 1000000, ad5755, 4),
    M(0xced9, "mPCIe-DAAI16-4A", 1, 16, 500000, ad5755, 4),
    M(0xceda, "mPCIe-DAAI16-4E", 1, 16, 250000, ad5755, 4),
    M(0xce98, "mPCIe-DAAI12-4A", 1, 12, 500000, ad5755, 4),
    M(0xce9a, "mPCIe-DAAI12-4", 1, 12, 250000, ad5755, 4),
    M(0xce9b, "mPCIe-DAAI12-4E", 1, 12, 100000, ad5755, 4),
};
#undef M

int status_error(int status) noexcept {
    if (status == 0) return 0;
    return status < 0 && errno ? errno : EIO;
}

class ApciBackend final : public Backend {
public:
    ApciBackend(int descriptor, const Model &model, std::string path)
        : fd_(descriptor), model_(model), path_(std::move(path)) {
        dac_ranges_.fill(rcNoChange);
        const char *setting = std::getenv("AIOAIO_DEBUG");
        debug_ = setting && std::strcmp(setting, "1") == 0;
        trace("opened %s, PCI device ID 0x%04x", model_.name, model_.device_id);
    }
    ~ApciBackend() override {
        if (touched_) (void)stop_adc();
        (void)dac_stop();
        (void)unmap_dac();
        unmap();
        // APCI owns the coherent allocation beyond close. Do not resize it to
        // zero: that ioctl assumes a valid nonzero ring and can race its ISR.
        ::close(fd_);
    }

    const Model &model() const noexcept override { return model_; }
    const std::string &path() const noexcept override { return path_; }

    int read_register(uint32_t width, uint32_t offset, uint32_t &value) override {
        value = 0;
        int error = validate_register(width, offset);
        if (error) return error;
        if (running_ || dac_prepared_ || dac_running_) return EBUSY;
        return raw_read(width, offset, value);
    }
    int write_register(uint32_t width, uint32_t offset, uint32_t value) override {
        int error = validate_register(width, offset);
        if (error) return error;
        if ((error = allow_raw_write())) return error;
        return raw_write(width, offset, value);
    }
    int read_register_block(uint32_t width, uint32_t offset, uint32_t count, void *data) override {
        int error = validate_block(width, offset, count, data);
        if (error) return error;
        if (running_ || dac_prepared_ || dac_running_) return EBUSY;
        auto *output = static_cast<unsigned char *>(data);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t value = 0;
            if ((error = raw_read(width, offset, value))) return error;
            if (width == 1) { const uint8_t v = static_cast<uint8_t>(value); std::memcpy(output, &v, 1); }
            else if (width == 2) { const uint16_t v = static_cast<uint16_t>(value); std::memcpy(output, &v, 2); }
            else std::memcpy(output, &value, 4);
            output += width;
        }
        return 0;
    }
    int write_register_block(uint32_t width, uint32_t offset, uint32_t count, const void *data) override {
        int error = validate_block(width, offset, count, data);
        if (error) return error;
        if (!count) return running_ || dac_prepared_ || dac_running_ ? EBUSY : 0;
        if ((error = allow_raw_write())) return error;
        const auto *input = static_cast<const unsigned char *>(data);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t value = 0;
            if (width == 1) { uint8_t v; std::memcpy(&v, input, 1); value = v; }
            else if (width == 2) { uint16_t v; std::memcpy(&v, input, 2); value = v; }
            else std::memcpy(&value, input, 4);
            if ((error = raw_write(width, offset, value))) return error;
            input += width;
        }
        return 0;
    }

    int reset_board() override {
        // The owner has already stopped/joined its workers. Reset is also the
        // recovery path after failed cleanup or manual register programming.
        if (running_ || dac_running_) return EBUSY;
        const bool dac_cleanup_pending = dac_touched_ || dac_prepared_;
        int first_error = 0;
        auto remember = [&](int error) { if (!first_error) first_error = error; };
        dac_ranges_.fill(rcNoChange);
        configured_ = false;
        errno = 0;
        remember(status_error(apci_write8(fd_, 0, 0, 0x1c, 8))); // Abort DMA first.
        uint32_t ignored = 0;
        remember(read_bar(0, 0x1c, ignored));
        const int reset_error = write(0, 1u); // Documented self-clearing whole-board reset.
        remember(reset_error);
        if (!reset_error) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            for (;;) {
                uint32_t state = 0;
                const int error = read(0, state);
                if (error) { remember(error); break; }
                if (!(state & 1u)) break;
                if (std::chrono::steady_clock::now() >= deadline) { remember(ETIMEDOUT); break; }
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
            // Device initialization is not complete merely because the reset
            // request cleared. Allow ADAS reset recovery before another command.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (model_.dac_count) remember(wait_dac_spi());
        }
        remember(unmap());
        remember(unmap_dac());
        config_ = Config{};
        divisor_ = control_ = 0;
        actual_rate_ = 0;
        reset_failed_ = first_error != 0;
        // Keep failed cleanup owned so Close can retry stopping hardware. A
        // failed reset also blocks new managed/raw writes until Reset succeeds.
        touched_ = reset_failed_;
        running_ = false;
        dac_touched_ = reset_failed_ && dac_cleanup_pending;
        dac_prepared_ = dac_running_ = dac_use_irq_ = false;
        if (!reset_failed_) dac_channels_ = dac_fifo_samples_ = dac_divisor_ = 0;
        return first_error;
    }

    int immediate_scan(const Config &config, uint32_t *words, uint32_t &count) override {
        count = 0;
        if (!words) return EINVAL;
        if (reset_failed_) return EIO;
        if (running_ || dac_prepared_ || dac_running_ || dac_touched_) return EBUSY;
        const uint32_t per_adc = config.differential ? 4u : 8u;
        for (uint32_t adc = 0; adc < model_.adc_count; ++adc)
            for (uint32_t ch = 0; ch < per_adc; ++ch) {
                const uint32_t gain = config.range_codes[adc * 8 + (config.differential ? ch * 2 : ch)];
                if (gain > 7 || gain == 6) return EINVAL;
            }
        uint32_t previous_control[2]{}, previous_divisor = 0, previous_threshold = 0, previous_irq = 0;
        uint32_t dio = 0;
        int error = read(dio_control_offset, dio);
        if (error) return error;
        if (dio & ((1u << 16) | (1u << 18))) return ENOTSUP;
        for (uint32_t adc = 0; adc < model_.adc_count; ++adc) {
            if ((error = read(control_offset + adc * 4, previous_control[adc]))) return error;
            if (previous_control[adc] & go) return EBUSY;
        }
        if ((error = read(divisor_offset, previous_divisor)) ||
            (error = read(threshold_offset, previous_threshold)) ||
            (error = read(irq_offset, previous_irq))) return error;
        std::array<uint32_t, 16> result{};
        int first_error = 0;
        auto remember = [&](int e) { if (!first_error) first_error = e; };
        // Suppress ADC IRQs while using the FIFO directly. High bits are W1C;
        // preserve unrelated interrupt statuses and enable bits throughout.
        remember(write(irq_offset, (previous_irq & 0xffffu & ~adc_irq_mask) | (adc_irq_mask << 16)));
        uint32_t flushed_irq = 0;
        if (!first_error) remember(read(irq_offset, flushed_irq));
        if (!first_error && (flushed_irq & adc_irq_mask)) remember(EIO);
        if (!first_error) remember(write(divisor_offset, 0));
        if (!first_error) remember(clear_fifo());
        for (uint32_t adc = 0; adc < model_.adc_count && !first_error; ++adc) {
            for (uint32_t ch = 0; ch < per_adc && !first_error; ++ch) {
                const uint32_t physical = config.differential ? ch * 2 : ch;
                const uint32_t gain = config.range_codes[adc * 8 + physical];
                // CFG, normal mux/temp, REFEN, CPHA and slow-conversion CMS;
                // SEQ=00 and SCAN=0 select one software-started conversion.
                const uint32_t command = 0x804fu | (config.differential ? 0u : 1u << 11) |
                    (physical << 12) | (gain << 7);
                remember(write(control_offset + adc * 4, command | load_config | go));
                if (first_error) break;
                // Windows native sequence requires >=25us between software
                // starts; GO clears earlier, so GO alone is not readiness.
                std::this_thread::sleep_for(std::chrono::microseconds(30));
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
                uint32_t depth = 0;
                for (;;) {
                    error = read(fifo_count_offset, depth);
                    if (error || depth) break;
                    if (std::chrono::steady_clock::now() >= deadline) { error = ETIMEDOUT; break; }
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
                remember(error);
                if (first_error) break;
                if (depth != 1) { remember(EIO); break; }
                uint32_t value = 0;
                remember(read(0x30, value));
                if (first_error) break;
                // Native software mode counts individual 32-bit words. Do not
                // apply the timed-DMA two-words-per-FIFO-row interpretation.
                if ((value & 0x80000000u) || ((value >> 20) & 7u) != physical ||
                    (model_.adc_count == 2 && ((value >> 23) & 1u) != adc) ||
                    ((value >> 16) & 7u) != gain ||
                    (((value >> 19) & 1u) != (config.differential ? 1u : 0u))) {
                    remember(EIO); break;
                }
                result[adc * per_adc + ch] = value;
            }
        }
        // Restore every changed persistent setting even after partial setup.
        // GO is never restored: an active acquisition was rejected on entry.
        for (uint32_t adc = 0; adc < model_.adc_count; ++adc)
            remember(write(control_offset + adc * 4, previous_control[adc] & ~go & ~load_config));
        remember(clear_fifo());
        remember(write(threshold_offset, previous_threshold));
        for (uint32_t adc = 0; adc < model_.adc_count; ++adc) {
            remember(write(control_offset + adc * 4, (previous_control[adc] & ~go) | load_config));
            std::this_thread::sleep_for(std::chrono::microseconds(30));
        }
        remember(write(divisor_offset, previous_divisor));
        remember(write(irq_offset, (previous_irq & 0xffffu) | (adc_irq_mask << 16)));
        if (first_error) { configured_ = false; return first_error; }
        count = per_adc * model_.adc_count;
        std::copy_n(result.begin(), count, words);
        return 0;
    }

    int configure(const Config &config, double &actual_rate) override {
        if (reset_failed_) return EIO;
        if (running_ || dac_prepared_) return EBUSY;
        const uint32_t limit = config.differential ? 4 : 8;
        if (config.channels_per_adc == 0 || config.channels_per_adc > limit ||
            !std::isfinite(config.rate_hz) || config.rate_hz <= 0) return EINVAL;
        if (config.rate_hz > model_.max_adc_rate_hz) {
            trace("requested ADC rate %.9g exceeds %s maximum of %u conversions/s/ADC",
                  config.rate_hz, model_.name, model_.max_adc_rate_hz);
            return ERANGE;
        }
        if (debug_ && !reported_firmware_) {
            // Diagnostics must not turn an optional version read into an
            // acquisition prerequisite, especially on older firmware.
            uint32_t version = 0;
            errno = 0;
            const int error = status_error(apci_read32(fd_, 0, registers, 0x68, &version));
            if (error) trace("FPGA revision at logical BAR[1]+0x68: unavailable: %s (%d)",
                             std::strerror(error), error);
            else trace("FPGA revision at logical BAR[1]+0x68: 0x%08x (raw)", version);
            reported_firmware_ = true;
        }
        for (uint32_t adc = 0; adc < model_.adc_count; ++adc)
            for (uint32_t ch = 0; ch < config.channels_per_adc; ++ch) {
                const uint32_t input = config.differential ? ch * 2 : ch;
                const uint32_t range = config.range_codes[adc * 8 + input];
                if (range > 7 || range == 6) return EINVAL;
            }
        uint32_t base_clock = 0;
        int error = read(clock_offset, base_clock);
        if (error) return error;
        if (!base_clock) {
            trace("FAILED ADC base clock at logical BAR[1]+0x0c: expected a nonzero clock, got 0");
            return EIO;
        }
        const double rounded = std::round(static_cast<double>(base_clock) / config.rate_hz);
        if (rounded < 1 || rounded > std::numeric_limits<uint32_t>::max()) return ERANGE;
        // Round to the nearest divider unless that would exceed this model's
        // rated rate. This also handles clocks not divisible by the maximum.
        const uint32_t minimum_divisor = static_cast<uint32_t>(
            (static_cast<uint64_t>(base_clock) + model_.max_adc_rate_hz - 1) / model_.max_adc_rate_hz);
        config_ = config;
        divisor_ = std::max(static_cast<uint32_t>(rounded), minimum_divisor);
        actual_rate = static_cast<double>(base_clock) / divisor_;
        actual_rate_ = actual_rate;
        configured_ = true;
        return 0;
    }

    int prepare_dma(uint32_t slots, uint32_t rows) override {
        trace("prepare DMA: %u slots, %u FIFO rows per slot", slots, rows);
        if (!configured_) return EINVAL;
        if (running_ || dac_prepared_) return EBUSY;
        if (slots < 2 || slots > 255 || rows == 0 || rows > model_.max_fifo_rows)
            return EINVAL;
        // Reject a board already being driven by a noncooperating application.
        uint32_t control = 0;
        int error = read(control_offset, control);
        if (error) return error;
        if (control & go) return EBUSY;
        uint32_t dio_control = 0;
        if ((error = read(dio_control_offset, dio_control))) return error;
        // External-start/trigger configuration changes the raw DMA format and
        // timing contract. Do not silently alter the application's DIO setup.
        if (dio_control & ((1u << 16) | (1u << 18))) return ENOTSUP;
        touched_ = true;
        if ((error = stop_adc())) return error;
        if ((error = unmap())) return error;
        const size_t slot_bytes = static_cast<size_t>(rows) * model_.raw_words_per_row * sizeof(uint32_t);
        errno = 0;
        error = status_error(apci_dma_transfer_size(fd_, 0, static_cast<__u8>(slots), slot_bytes));
        if (error) {
            trace("FAILED apci_dma_transfer_size: %s (%d)", std::strerror(error), error);
            return error;
        }
        const size_t total_bytes = slot_bytes * slots;
        void *memory = ::mmap(nullptr, total_bytes, PROT_READ, MAP_SHARED, fd_, 0);
        if (memory == MAP_FAILED) {
            error = errno ? errno : EIO;
            trace("FAILED mmap DMA (%zu bytes): %s (%d)", total_bytes, std::strerror(error), error);
            return error;
        }
        mapping_ = memory;
        mapping_bytes_ = total_bytes;
        slot_bytes_ = slot_bytes;
        slots_ = slots;
        rows_ = rows;
        return 0;
    }

    int start_adc() override {
        trace("start ADC");
        if (running_ || dac_prepared_) return EBUSY;
        if (!configured_ || mapping_ == MAP_FAILED) return EINVAL;
        int error = 0;
        if ((error = clear_fifo())) return error;
        if ((error = write(threshold_offset, rows_))) return error;
        uint32_t value = 0;
        if ((error = read(threshold_offset, value))) return error;
        if ((error = check_readback("ADC FIFO threshold", threshold_offset, value, rows_, 0xffffffffu, ERANGE))) return error;
        if ((error = write(divisor_offset, divisor_))) return error;
        if ((error = read(divisor_offset, value))) return error;
        if ((error = check_readback("ADC clock divisor", divisor_offset, value, divisor_))) return error;

        // Native FPGA channel mode (bit18=0) clocks one conversion/ADC per
        // divisor. ADAS advanced sequencing still cycles inputs; +0x14 is
        // ignored in this FPGA mode (native register-map description).
        for (uint32_t adc = 0; adc < model_.adc_count; ++adc) {
            uint32_t gains = 0;
            for (uint32_t ch = 0; ch < config_.channels_per_adc; ++ch) {
                const uint32_t input = config_.differential ? ch * 2 : ch;
                gains |= config_.range_codes[adc * 8 + input] << (input * 4);
            }
            if ((error = write(gains_offset + adc * 4, gains))) return error;
            if ((error = read(gains_offset + adc * 4, value))) return error;
            if ((error = check_readback("ADC gain", gains_offset + adc * 4, value, gains))) return error;
        }
        const uint32_t last = (config_.channels_per_adc - 1) * (config_.differential ? 2 : 1);
        // CFG, MUX, advanced SEQ, TEMPB, REFEN and CPHA. CMS=normal at <=900k.
        control_ = 0x0000806d | (last << 12);
        if (!config_.differential) control_ |= 1u << 11;
        if (actual_rate_ <= 900000.0) control_ |= 1u << 1;
        // ADC1's independent lower configuration must precede ADC0. Timed
        // acquisitions mirror ADC0's high control bits onto ADC1 in hardware.
        if (model_.adc_count == 2)
            if ((error = write(control_offset + 4, control_))) return error;
        if ((error = write(control_offset, control_ | load_config))) return error;
        if ((error = read(control_offset, value))) return error;
        constexpr uint32_t configuration_mask = 0x00007bfa;
        if ((error = check_readback("ADC configuration", control_offset, value, control_, configuration_mask))) return error;
        if (model_.adc_count == 2) {
            if ((error = read(control_offset + 4, value))) return error;
            if ((error = check_readback("ADC configuration", control_offset + 4, value, control_, configuration_mask))) return error;
        }
        uint32_t irq = 0;
        if ((error = read(irq_offset, irq))) return error;
        // High status bits are W1C: clear only acquisition-owned statuses.
        if ((error = write(irq_offset, (irq & 0xffffu & ~adc_irq_mask) |
                                      dma_irqs | (adc_irq_mask << 16)))) return error;
        running_ = true; // Also clean up a partially successful GO write.
        error = write(control_offset, control_ | load_config | go);
        if (error) { (void)stop_adc(); return error; }
        if ((error = read(control_offset, value))) { (void)stop_adc(); return error; }
        if ((error = check_readback("ADC GO", control_offset, value, control_ | load_config | go, go))) {
            (void)stop_adc();
            return error;
        }
        return 0;
    }

    int stop_adc() override {
        if (!touched_) return 0;
        trace("stop ADC");
        int first_error = 0;
        auto remember = [&first_error](int error) { if (!first_error) first_error = error; };
        uint32_t value = 0;
        int error = read(control_offset, value);
        remember(error);
        if (!error) remember(write(control_offset, value & ~go & ~load_config));
        error = read(irq_offset, value);
        remember(error);
        if (!error) remember(write(irq_offset, (value & 0xffffu & ~adc_irq_mask) |
                                             (adc_irq_mask << 16)));
        // BAR0+0x1c bit3 aborts ADC DMA and clears its FIFO. This is the
        // operation used by APCI itself in apci_set_dma_transfer_size.
        errno = 0;
        error = status_error(apci_write8(fd_, 0, 0, 0x1c, 0x08));
        trace("%swrite8 logical BAR[0]+0x1c = 0x08: %s (%d)",
              error ? "FAILED " : "", error ? std::strerror(error) : "ok", error);
        remember(error);
        remember(read_bar(0, 0x1c, value)); // flush posted write
        remember(clear_fifo());
        running_ = false;
        return first_error;
    }

    int ready(uint32_t &start, uint32_t &count, uint32_t &discarded) override {
        start = count = discarded = 0;
        if (mapping_ == MAP_FAILED) return EINVAL;
        int s = 0, n = 0, d = 0;
        errno = 0;
        const int error = status_error(apci_dma_data_ready(fd_, 0, &s, &n, &d));
        if (error) return error;
        if (n < 0 || d < 0 || static_cast<uint32_t>(n) >= slots_ ||
            (n && (s < 0 || static_cast<uint32_t>(s) >= slots_))) return EIO;
        start = n ? static_cast<uint32_t>(s) : 0;
        count = static_cast<uint32_t>(n);
        discarded = static_cast<uint32_t>(d);
        outstanding_ = count;
        return 0;
    }

    int acknowledge(uint32_t count) override {
        if (count == 0) return 0;
        if (count > outstanding_ || count >= slots_) return EINVAL;
        errno = 0;
        const int error = status_error(apci_dma_data_done(fd_, 0, static_cast<int>(count)));
        if (!error) outstanding_ -= count;
        return error;
    }

    const uint32_t *slot(uint32_t index) const noexcept override {
        if (mapping_ == MAP_FAILED || index >= slots_) return nullptr;
        return reinterpret_cast<const uint32_t *>(static_cast<const unsigned char *>(mapping_) + slot_bytes_ * index);
    }
    int wait_irq() override {
        errno = 0;
        return status_error(apci_wait_for_irq(fd_, 0));
    }
    int cancel_irq() override {
        errno = 0;
        return status_error(apci_cancel_irq(fd_, 0));
    }

    int dac_set_range(uint32_t channel, uint32_t range) override {
        int error = validate_dac_channel(channel);
        if (error) return error;
        if (range == rcNoChange) return immediate_dac_ready();
        if ((error = normalize_dac_range(range))) return error;
        if ((error = immediate_dac_ready())) return error;
        if (channel == ciAll && model_.dac_chip == DacChip::ltc2664) {
            // A transport failure may occur after the command reached hardware.
            dac_ranges_.fill(rcNoChange);
            error = dac_spi_command(0x00e00000u | range);
            if (!error) dac_ranges_.fill(range);
            return error;
        }
        const uint32_t first = channel == ciAll ? 0 : channel;
        const uint32_t end = channel == ciAll ? model_.dac_count : channel + 1;
        for (uint32_t ch = first; ch < end; ++ch)
            if ((error = set_dac_range(ch, range))) return error;
        return 0;
    }

    int dac_set_ranges(uint32_t count, const uint32_t *ranges) override {
        int error = validate_dac_channel(ciAll);
        if (error) return error;
        if (!count) return immediate_dac_ready(); // Windows-compatible no-op.
        if (!ranges) return EINVAL;
        count = std::min(count, model_.dac_count);
        std::array<uint32_t, 8> normalized{};
        // Validate every supplied range before changing any channel. The last
        // supplied range extends to the remaining channels, including NoChange.
        for (uint32_t ch = 0; ch < model_.dac_count; ++ch) {
            normalized[ch] = ranges[std::min(ch, count - 1)];
            if (normalized[ch] != rcNoChange &&
                (error = normalize_dac_range(normalized[ch]))) return error;
        }
        if ((error = immediate_dac_ready())) return error;
        for (uint32_t ch = 0; ch < model_.dac_count; ++ch)
            if (normalized[ch] != rcNoChange &&
                (error = set_dac_range(ch, normalized[ch]))) return error;
        return 0;
    }

    int dac_output_counts(uint32_t channel, uint64_t counts) override {
        int error = validate_dac_channel(channel);
        if (error) return error;
        if (counts > 0xffffu) return ERANGE;
        if ((error = immediate_dac_ready())) return error;
        if (channel == ciAll && model_.dac_chip == DacChip::ltc2664)
            return dac_spi_command(0x00a00000u | static_cast<uint32_t>(counts));
        const uint32_t first = channel == ciAll ? 0 : channel;
        const uint32_t end = channel == ciAll ? model_.dac_count : channel + 1;
        for (uint32_t ch = first; ch < end; ++ch)
            if ((error = write_dac_counts(ch, static_cast<uint32_t>(counts)))) return error;
        return 0;
    }

    int dac_output_value(uint32_t channel, double value) override {
        int error = validate_dac_channel(channel);
        if (error) return error;
        if (!std::isfinite(value)) return EINVAL;
        const uint32_t first = channel == ciAll ? 0 : channel;
        const uint32_t end = channel == ciAll ? model_.dac_count : channel + 1;
        std::array<uint32_t, 8> counts{};
        // Prevalidate the entire broadcast: no output changes if a later
        // channel has unknown range or cannot represent the requested value.
        for (uint32_t ch = first; ch < end; ++ch)
            if ((error = dac_value_to_counts(ch, value, counts[ch]))) return error;
        if ((error = immediate_dac_ready())) return error;
        for (uint32_t ch = first; ch < end; ++ch)
            if ((error = write_dac_counts(ch, counts[ch]))) return error;
        return 0;
    }

    int dac_capabilities(DacCapabilities &caps) override {
        caps = {};
        if (!has_dac_waveform()) return ENOTSUP;
        uint32_t depth = 0, version = 0;
        int error = read(dac_fifo_size_offset, depth);
        if (error) return error;
        // Occupancy has fourteen bits; zero or an impossible capacity is not
        // evidence of a supported FIFO on older/different FPGA firmware.
        if (depth == 0 || depth > 0x3fffu) return ENOTSUP;
        if ((error = read(0x68, version))) return error;
        caps = {4, depth, version, dac_scalar_rate};
        return 0;
    }

    int dac_prepare(uint32_t channels, double rate, bool irq, double &actual) override {
        actual = 0;
        if (reset_failed_) return EIO;
        if (!has_dac_waveform()) return ENOTSUP;
        if (running_ || dac_running_) return EBUSY;
        if (!channels || channels > 4 || !std::isfinite(rate) || rate <= 0) return EINVAL;
        if (rate > dac_scalar_rate / channels) return ERANGE;
        DacCapabilities caps{};
        int error = dac_capabilities(caps);
        if (error) return error;
        uint32_t clock = 0, divider = 0, dio = 0;
        if ((error = read(clock_offset, clock))) return error;
        if (!clock) return EIO;
        const double rounded = std::round(static_cast<double>(clock) / rate);
        if (rounded < 1 || rounded > std::numeric_limits<uint32_t>::max()) return ERANGE;
        const uint32_t minimum = std::max(50u * channels, static_cast<uint32_t>(
            (static_cast<uint64_t>(clock) * channels + 999999u) / 1000000u));
        const uint32_t desired = std::max(static_cast<uint32_t>(rounded), minimum);
        if ((error = read(dio_control_offset, dio))) return error;
        // Divider zero can still consume FIFO through external LDAC, and an
        // external waveform trigger changes when a nonzero divider starts.
        if (dio & dac_external_modes) return ENOTSUP;
        if ((error = read(dac_divisor_offset, divider))) return error;
        if (divider && !dac_touched_) return EBUSY;
        if (dac_touched_ && (error = dac_stop())) return error;

        // This allocation belongs to APCI, not the waveform vector. Always
        // remove the old mapping BEFORE an ioctl can replace its backing pages.
        if ((error = unmap_dac())) return error;
        const long page = ::sysconf(_SC_PAGESIZE);
        if (page <= 0) return EIO;
        const size_t page_bytes = static_cast<size_t>(page);
        const size_t bytes = ((dac_staging_target + page_bytes - 1) / page_bytes) * page_bytes;
        errno = 0;
        if ((error = status_error(apci_dac_buffer_size(fd_, bytes)))) return error;
        void *memory = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, page);
        if (memory == MAP_FAILED) return errno ? errno : EIO;
        dac_mapping_ = memory;
        dac_mapping_bytes_ = bytes;
        dac_fifo_samples_ = caps.fifo_samples;
        dac_touched_ = true;
        // Bring only the DAC subsystem to an idle, empty, zero-volt state.
        if ((error = dac_stop())) return error;
        dac_touched_ = true;
        if ((error = write(dac_channels_offset, channels))) return error;
        uint32_t value = 0;
        if ((error = read(dac_channels_offset, value))) return error;
        if ((error = check_readback("DAC channel count", dac_channels_offset, value, channels, 0xfu))) return error;
        dac_channels_ = channels;
        dac_divisor_ = desired;
        dac_use_irq_ = irq;
        dac_prepared_ = true;
        actual = static_cast<double>(clock) / desired;
        return 0;
    }

    int dac_upload(const uint32_t *words, uint32_t count) override {
        if (!has_dac_waveform()) return ENOTSUP;
        if (!dac_prepared_ || dac_mapping_ == MAP_FAILED) return EINVAL;
        if (!count) return 0;
        if (!words || count % dac_channels_) return EINVAL;
        if (count > dac_fifo_samples_) return ENOSPC;
        // Only tagged count words are accepted; no embedded LTC2664 commands.
        for (uint32_t i = 0; i < count; ++i)
            if ((words[i] >> 16) >= dac_channels_) return EINVAL;
        uint32_t queued = 0;
        bool empty = false;
        int error = dac_state(queued, empty);
        if (error) return error;
        // A refill must not hide an already observed gap in running playback.
        // Preloading an idle FIFO is the only valid empty-FIFO upload.
        if (dac_running_ && (empty || queued == 0)) return EPIPE;
        if (count > dac_fifo_samples_ - queued) return ENOSPC;
        const uint32_t staging_words = static_cast<uint32_t>(dac_mapping_bytes_ / sizeof(uint32_t));
        uint32_t uploaded = 0;
        while (uploaded < count) {
            const uint32_t batch = std::min(count - uploaded, staging_words);
            std::memcpy(dac_mapping_, words + uploaded, static_cast<size_t>(batch) * sizeof(uint32_t));
            errno = 0;
            error = status_error(apci_writebuf32(fd_, 0, registers, dac_fifo_offset, 0, static_cast<int>(batch)));
            if (error) return error; // Caller stops: an ioctl may have written a prefix.
            uploaded += batch;
        }
        return 0;
    }

    int dac_start() override {
        if (!has_dac_waveform()) return ENOTSUP;
        if (running_ || dac_running_) return EBUSY;
        if (!dac_prepared_) return EINVAL;
        uint32_t queued = 0;
        bool empty = false;
        int error = dac_state(queued, empty);
        if (error) return error;
        if (empty || queued < dac_channels_) return EAGAIN;
        if ((error = set_dac_irq(dac_use_irq_))) { (void)dac_stop(); return error; }
        dac_running_ = true; // A failed write can have reached the hardware.
        if ((error = write(dac_divisor_offset, dac_divisor_))) { (void)dac_stop(); return error; }
        uint32_t value = 0;
        if ((error = read(dac_divisor_offset, value)) ||
            (error = check_readback("DAC clock divisor", dac_divisor_offset, value, dac_divisor_))) {
            (void)dac_stop();
            return error;
        }
        return 0;
    }

    int dac_state(uint32_t &queued, bool &empty) override {
        queued = 0;
        empty = true;
        if (!has_dac_waveform()) return ENOTSUP;
        if (!dac_fifo_samples_) return EINVAL;
        uint32_t count = 0, status = 0;
        int error = read(dac_fifo_offset, count);
        if (error) return error;
        if (count > dac_fifo_samples_) return EIO;
        if ((error = read(dac_command_offset, status))) return error;
        queued = count;
        empty = (status & dac_empty) != 0;
        return 0;
    }

    int dac_stop() override {
        if (!dac_touched_) return 0;
        int first_error = 0;
        auto remember = [&first_error](int error) { if (!first_error) first_error = error; };
        remember(set_dac_irq(false));
        // Current FDS HDL clears the FIFO on a nonzero -> zero divider change.
        remember(write(dac_divisor_offset, 0));
        uint32_t divider = 0;
        int error = read(dac_divisor_offset, divider);
        remember(error);
        const bool stopped = !error && divider == 0;
        if (!error && !stopped) remember(EIO);
        if (stopped) {
            remember(clear_dac_fifo());
            // The waveform interface owns all four DAC ranges/outputs. Keep
            // unused channels at zero as well, including after partial setup.
            dac_ranges_.fill(rcNoChange);
            error = dac_spi_command(0x00e00003u); // +/-10V span on all DACs.
            if (!error) dac_ranges_.fill(3u);
            remember(error);
            if (!error) remember(dac_spi_command(0x00a08000u)); // All outputs 0V.
        }
        dac_prepared_ = false;
        dac_running_ = false;
        if (!first_error) dac_touched_ = false;
        return first_error;
    }

private:
    static int validate_register(uint32_t width, uint32_t offset) noexcept {
        // Native production AxIO register BARs have a 4KiB aperture (BAR1 in
        // APCI's logical numbering, including physical64-bit BAR2 mappings).
        // APCI's scalar ioctl bounds check does not include the access width.
        if (width != 1 && width != 2 && width != 4) return EINVAL;
        if (offset % width || offset > 4096u - width) return EINVAL;
        return 0;
    }
    static int validate_block(uint32_t width, uint32_t offset, uint32_t count, const void *data) noexcept {
        const int error = validate_register(width, offset);
        if (error) return error;
        if (count && !data) return EINVAL;
        if (count > static_cast<uint32_t>(std::numeric_limits<int>::max()) / width ||
            count > std::numeric_limits<size_t>::max() / width) return EOVERFLOW;
        return 0;
    }
    int allow_raw_write() {
        if (reset_failed_) return EIO;
        if (running_ || dac_prepared_ || dac_running_ || dac_touched_) return EBUSY;
        // Raw register ownership bypasses the configured ADC/DAC state. A write
        // can reach hardware even when the transport subsequently reports failure.
        configured_ = false;
        dac_ranges_.fill(rcNoChange);
        // Completed ADC cleanup must not run again at Close and overwrite a
        // user's subsequent manual register configuration.
        touched_ = false;
        return 0;
    }
    int raw_read(uint32_t width, uint32_t offset, uint32_t &value) {
        errno = 0;
        int status = 0;
        if (width == 1) { uint8_t v = 0; status = apci_read8(fd_, 0, registers, offset, &v); value = v; }
        else if (width == 2) { uint16_t v = 0; status = apci_read16(fd_, 0, registers, offset, &v); value = v; }
        else status = apci_read32(fd_, 0, registers, offset, &value);
        const int error = status_error(status);
        if (error) value = 0;
        return error;
    }
    int raw_write(uint32_t width, uint32_t offset, uint32_t value) {
        // The present APCI ISR assumes a configured DMA ring for these sources.
        // Do not allow a raw enable to make it index an unallocated ring.
        if (offset == irq_offset && (value & 5u) && !slots_) return EBUSY;
        errno = 0;
        if (width == 1) return status_error(apci_write8(fd_, 0, registers, offset, static_cast<uint8_t>(value)));
        if (width == 2) return status_error(apci_write16(fd_, 0, registers, offset, static_cast<uint16_t>(value)));
        return status_error(apci_write32(fd_, 0, registers, offset, value));
    }
    int validate_dac_channel(uint32_t channel) const noexcept {
        if (model_.dac_chip == DacChip::none || !model_.dac_count) return ENOTSUP;
        return channel != ciAll && channel >= model_.dac_count ? EINVAL : 0;
    }
    int normalize_dac_range(uint32_t &range) const noexcept {
        switch (range) {
        case rcUnipolar5: range = 0; break;
        case rcUnipolar10: range = 1; break;
        case rcBipolar5: range = 2; break;
        case rcBipolar10: range = 3; break;
        case rcBipolar2_5:
            if (model_.dac_chip != DacChip::ltc2664) return EINVAL;
            range = 4; break;
        case rcCurrent420: case rcCurrent020: case rcCurrent024:
            if (model_.dac_chip != DacChip::ad5755) return EINVAL;
            range = range == rcCurrent420 ? 4 : range == rcCurrent020 ? 5 : 6;
            break;
        default: break;
        }
        return range <= (model_.dac_chip == DacChip::ad5755 ? 6u : 4u) ? 0 : EINVAL;
    }
    int immediate_dac_ready() {
        if (reset_failed_) return EIO;
        if (running_ || dac_prepared_ || dac_running_ || dac_touched_) return EBUSY;
        uint32_t divider = 0, dio = 0;
        int error = 0;
        // +08 was V/I-detect on older non-FDS LTC2664 maps; only the FDS
        // and AD5755 maps establish that a nonzero value means timed output.
        if (has_dac_waveform() || model_.dac_chip == DacChip::ad5755) {
            if ((error = read(dac_divisor_offset, divider))) return error;
            if (divider) return EBUSY;
        }
        if ((error = read(dio_control_offset, dio))) return error;
        // External LDAC is shared by the LTC2664 and AD5755 maps. Bit27 is
        // only documented as an external waveform trigger on the FDS maps.
        const uint32_t external = (1u << 20) | (has_dac_waveform() ? (1u << 27) : 0);
        if (dio & external) return ENOTSUP;
        return 0;
    }
    int ad5755_command(uint32_t command, uint32_t channel, uint32_t data) {
        const uint32_t chip = channel / 4 + 1;
        // The AD5755 FPGA controller holds SPIbusy through its inter-command
        // delay as well as the wire transfer; pre/post busy waits honor both.
        return dac_spi_command((chip << 21) | (command << 18) |
                               ((channel % 4) << 16) | data);
    }
    int set_dac_range(uint32_t channel, uint32_t range) {
        dac_ranges_[channel] = rcNoChange;
        int error = 0;
        if (model_.dac_chip == DacChip::ad5755) {
            if ((error = ad5755_command(7, channel, 0x4110u | range))) return error;
            // Preload zero volts (or the current range's lower endpoint) while
            // disabled, then allow the output amplifier to settle before enable.
            if ((error = ad5755_command(0, channel, range == 2 || range == 3 ? 0x8000u : 0u))) return error;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            error = ad5755_command(7, channel, 0x4150u | range);
        } else error = dac_spi_command(0x00600000u | (channel << 16) | range);
        if (!error) dac_ranges_[channel] = range;
        return error;
    }
    int write_dac_counts(uint32_t channel, uint32_t counts) {
        return model_.dac_chip == DacChip::ad5755 ? ad5755_command(0, channel, counts) :
            dac_spi_command(0x00300000u | (channel << 16) | counts);
    }
    int dac_value_to_counts(uint32_t channel, double value, uint32_t &counts) const noexcept {
        const uint32_t range = dac_ranges_[channel];
        if (range == rcNoChange) return ENODATA; // Never guess the hardware range.
        double low = 0, span = 0;
        switch (range) {
        case 0: span = 5; break;
        case 1: span = 10; break;
        case 2: low = -5; span = 10; break;
        case 3: low = -10; span = 20; break;
        case 4:
            if (model_.dac_chip == DacChip::ad5755) { low = 4; span = 16; }
            else { low = -2.5; span = 5; }
            break;
        case 5: span = 20; break;
        case 6: span = 24; break;
        default: return EIO;
        }
        if (value < low || value >= low + span) return ERANGE;
        const double scaled = value / span * 65536.0;
        const double offset = -low / span * 65536.0;
        const double floor = std::floor(scaled), fraction = scaled - floor;
        // Match Delphi Round (nearest, ties to even) regardless of the process
        // floating-point rounding mode. Positive full scale is not representable.
        const double rounded = floor + (fraction > 0.5 ||
            (fraction == 0.5 && std::fmod(floor, 2.0) != 0.0) ? 1.0 : 0.0);
        const double code = rounded + offset;
        if (code < 0.0 || code > 65535.0) return ERANGE;
        counts = static_cast<uint32_t>(code);
        return 0;
    }
    bool has_dac_waveform() const noexcept {
        return model_.device_id == 0xc0eb || model_.device_id == 0xc2ef || model_.device_id == 0xc0ef;
    }
    int set_dac_irq(bool enabled) {
        uint32_t value = 0;
        int error = read(irq_offset, value);
        if (error) return error;
        return write(irq_offset, (value & 0xffffu & ~dac_irq) |
                     (enabled ? dac_irq : 0u) | (dac_irq << 16));
    }
    int wait_dac_spi() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        do {
            uint32_t status = 0;
            const int error = read(dac_command_offset, status);
            if (error) return error;
            if (!(status & dac_spi_busy)) return 0;
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);
        return ETIMEDOUT;
    }
    int dac_spi_command(uint32_t command) {
        int error = wait_dac_spi();
        if (error) return error;
        if ((error = write(dac_command_offset, command))) return error;
        return wait_dac_spi();
    }
    int clear_dac_fifo() {
        uint32_t queued = 0;
        int error = read(dac_fifo_offset, queued);
        if (error) return error;
        if (!queued) return 0;
        // If a FIFO was preloaded while already stopped, no divider transition
        // occurred. The documented DAC-only reset clears that FIFO as well.
        // It also clears DAC IRQ enables and external-LDAC edge selection;
        // save/restore those fields without replaying any latched W1C status.
        // The hardware reset also clears a previously latched LDAC event.
        uint32_t resets = 0, irq = 0, dio = 0;
        if ((error = read(0, resets))) return error;
        if (resets & 0x3fu) return EBUSY;
        if ((error = read(irq_offset, irq))) return error;
        if ((error = read(dio_control_offset, dio))) return error;
        if (dio & dac_external_modes) return ENOTSUP;
        dac_ranges_.fill(rcNoChange); // The reset may reach hardware even if its write fails.
        int first_error = write(0, resets | 8u);
        auto remember = [&first_error](int e) { if (!first_error) first_error = e; };
        remember(write(0, resets & ~8u));
        remember(write(dio_control_offset, dio));
        remember(write(irq_offset, (irq & 0xffffu & ~dac_irq) | (dac_irq << 16)));
        remember(read(dac_fifo_offset, queued));
        if (!first_error && queued) return EIO;
        return first_error;
    }
    int unmap_dac() noexcept {
        if (dac_mapping_ != MAP_FAILED && ::munmap(dac_mapping_, dac_mapping_bytes_) < 0)
            return errno ? errno : EIO;
        dac_mapping_ = MAP_FAILED;
        dac_mapping_bytes_ = 0;
        return 0;
    }
    // Only setup/cleanup register traffic is traced, never sample contents.
    // Preserve errno so diagnostics cannot change an API's error result.
    void trace(const char *format, ...) const noexcept {
        if (!debug_) return;
        const int saved_errno = errno;
        ::flockfile(stderr);
        std::fprintf(stderr, "libaioaio [%s]: ", path_.c_str());
        va_list args;
        va_start(args, format);
        std::vfprintf(stderr, format, args);
        va_end(args);
        std::fputc('\n', stderr);
        ::funlockfile(stderr);
        errno = saved_errno;
    }
    int check_readback(const char *name, uint32_t offset, uint32_t actual,
                       uint32_t expected, uint32_t mask = 0xffffffffu,
                       int error = EIO) const noexcept {
        if ((actual & mask) == (expected & mask)) return 0;
        trace("FAILED %s at logical BAR[1]+0x%02x: expected 0x%08x, got 0x%08x, mask 0x%08x",
              name, offset, expected, actual, mask);
        return error;
    }
    int clear_fifo() {
        // Native AxIO maps define reset bit6 as ADC FIFO-only reset. Preserve
        // level controls such as DAAI HART reset; never assert SYS/DAC/DIO reset.
        // Some maps explicitly call bit6 self-clearing. Deassert it explicitly
        // as well so older implementations cannot leave the FIFO held reset.
        uint32_t resets = 0;
        int error = read(0, resets);
        if (error) return error;
        if (resets & 0x3fu) return EBUSY; // another reset/powerdown is active
        if ((error = write(0, resets | 0x40u))) return error;
        if ((error = write(0, resets & ~0x40u))) return error;
        uint32_t depth = 0;
        if ((error = read(fifo_count_offset, depth))) return error;
        return check_readback("ADC FIFO empty", fifo_count_offset, depth, 0);
    }
    int read(uint32_t offset, uint32_t &value) {
        return read_bar(registers, offset, value);
    }
    int read_bar(int bar, uint32_t offset, uint32_t &value) {
        errno = 0;
        const int error = status_error(apci_read32(fd_, 0, bar, static_cast<int>(offset), &value));
        if (error) trace("FAILED read32 logical BAR[%d]+0x%02x: %s (%d)",
                         bar, offset, std::strerror(error), error);
        else trace("read32 logical BAR[%d]+0x%02x = 0x%08x", bar, offset, value);
        return error;
    }
    int write(uint32_t offset, uint32_t value) {
        errno = 0;
        const int error = status_error(apci_write32(fd_, 0, registers, static_cast<int>(offset), value));
        trace("%swrite32 logical BAR[1]+0x%02x = 0x%08x: %s (%d)",
              error ? "FAILED " : "", offset, value, error ? std::strerror(error) : "ok", error);
        return error;
    }
    int unmap() noexcept {
        if (mapping_ != MAP_FAILED && ::munmap(mapping_, mapping_bytes_) < 0)
            return errno ? errno : EIO;
        mapping_ = MAP_FAILED;
        mapping_bytes_ = slot_bytes_ = 0;
        slots_ = rows_ = outstanding_ = 0;
        return 0;
    }
    int fd_;
    const Model &model_;
    std::string path_;
    Config config_;
    uint32_t divisor_ = 0, control_ = 0, slots_ = 0, rows_ = 0, outstanding_ = 0;
    double actual_rate_ = 0;
    void *mapping_ = MAP_FAILED;
    size_t mapping_bytes_ = 0, slot_bytes_ = 0;
    bool configured_ = false, touched_ = false, running_ = false, reset_failed_ = false;
    bool debug_ = false, reported_firmware_ = false;
    void *dac_mapping_ = MAP_FAILED;
    size_t dac_mapping_bytes_ = 0;
    uint32_t dac_channels_ = 0, dac_fifo_samples_ = 0, dac_divisor_ = 0;
    std::array<uint32_t, 8> dac_ranges_;
    bool dac_prepared_ = false, dac_touched_ = false, dac_running_ = false, dac_use_irq_ = false;
};

} // namespace

const Model *find_model(uint32_t id) noexcept {
    for (const auto &model : models) if (model.device_id == id) return &model;
    // Prototypes, DA-only and specialized DF/HC variants are deliberately
    // excluded until their ADC capability/range contract has been verified.
    return nullptr;
}

int open_backend(const std::string &path, std::unique_ptr<Backend> &backend) {
    if (path.empty() || path.find('\0') != std::string::npos) return EINVAL;
    int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) return errno;
    auto fail = [fd](int error) { ::close(fd); return error; };
    struct stat st{};
    if (::fstat(fd, &st) < 0) return fail(errno);
    if (!S_ISCHR(st.st_mode)) return fail(ENODEV);
    if (::flock(fd, LOCK_EX | LOCK_NB) < 0) return fail(errno == EWOULDBLOCK ? EBUSY : errno);
    unsigned int id = 0;
    errno = 0;
    const int error = status_error(apci_get_device_info(fd, 0, &id, nullptr));
    if (error) return fail(error);
    const Model *model = find_model(id);
    if (!model) return fail(ENOTSUP);
    try {
        backend = std::make_unique<ApciBackend>(fd, *model, path);
    } catch (...) {
        ::close(fd);
        throw;
    }
    return 0;
}

} // namespace aioaio
