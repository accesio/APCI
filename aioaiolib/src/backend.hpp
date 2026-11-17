#pragma once
#include "dac_backend.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace aioaio {

enum class DacChip { none, ltc2664, ad5755 };

struct Model {
    uint32_t device_id;
    const char *name;
    uint32_t adc_count;
    uint32_t channels_per_adc;
    uint32_t resolution_bits;
    uint32_t raw_words_per_row;
    uint32_t max_fifo_rows;
    uint32_t max_adc_rate_hz; // Conversions/s per physical ADC, shared by its channels.
    DacChip dac_chip = DacChip::none;
    uint32_t dac_count = 0;
};

struct Config {
    double rate_hz = 10000.0; // Conversion rate per physical ADC, shared by its channels.
    uint32_t channels_per_adc = 8;
    bool differential = false;
    std::array<uint32_t, 16> range_codes{};
};

// Errors are positive errno values. Instances have one owning acquisition worker;
// only cancel_irq() may run concurrently with wait_irq().
class Backend {
public:
    virtual ~Backend() = default;
    virtual const Model &model() const noexcept = 0;
    virtual const std::string &path() const noexcept = 0;
    virtual int configure(const Config &config, double &actual_rate_hz) = 0;
    virtual int prepare_dma(uint32_t slots, uint32_t rows_per_slot) = 0;
    virtual int start_adc() = 0;
    virtual int stop_adc() = 0;
    virtual int ready(uint32_t &start_slot, uint32_t &slot_count,
                      uint32_t &discarded_slots) = 0;
    virtual int acknowledge(uint32_t slot_count) = 0;
    virtual const uint32_t *slot(uint32_t index) const noexcept = 0;
    virtual int wait_irq() = 0;
    virtual int cancel_irq() = 0;
    // Exclusive, synchronous operations. Width is bytes (1, 2 or 4); block
    // transfers repeatedly access one register, as INS/OUTS do on Windows.
    virtual int reset_board() { return ENOTSUP; }
    virtual int read_register(uint32_t, uint32_t, uint32_t &) { return ENOTSUP; }
    virtual int write_register(uint32_t, uint32_t, uint32_t) { return ENOTSUP; }
    virtual int read_register_block(uint32_t, uint32_t, uint32_t, void *) { return ENOTSUP; }
    virtual int write_register_block(uint32_t, uint32_t, uint32_t, const void *) { return ENOTSUP; }
    // Results are packed ADC-major, 8 single-ended or 4 differential pairs per
    // ADC. Differential results represent physical inputs 0,2,4,6. The caller
    // provides room for 16 words. Temporary settings do not change Config.
    virtual int immediate_scan(const Config &, uint32_t *, uint32_t &) { return ENOTSUP; }
    // Immediate DAC operations require exclusive backend ownership. The range
    // cache lives with the register transport so waveform resets cannot leave
    // stale conversion state. Values are volts, or mA in AD5755 current ranges.
    virtual int dac_set_range(uint32_t, uint32_t) { return ENOTSUP; }
    virtual int dac_set_ranges(uint32_t, const uint32_t *) { return ENOTSUP; }
    virtual int dac_output_counts(uint32_t, uint64_t) { return ENOTSUP; }
    virtual int dac_output_value(uint32_t, double) { return ENOTSUP; }
    // Timed DAC playback shares this board's IRQ wait with ADC acquisition.
    // Default implementations keep ADC-only backends unsupported.
    virtual int dac_capabilities(DacCapabilities &) { return ENOTSUP; }
    virtual int dac_prepare(uint32_t, double, bool, double &) { return ENOTSUP; }
    virtual int dac_upload(const uint32_t *, uint32_t) { return ENOTSUP; }
    virtual int dac_start() { return ENOTSUP; }
    virtual int dac_state(uint32_t &, bool &) { return ENOTSUP; }
    virtual int dac_stop() { return 0; }
};

const Model *find_model(uint32_t device_id) noexcept;
int open_backend(const std::string &path, std::unique_ptr<Backend> &backend);

} // namespace aioaio
