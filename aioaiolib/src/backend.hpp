#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace aioaio {

struct Model {
    uint32_t device_id;
    const char *name;
    uint32_t adc_count;
    uint32_t channels_per_adc;
    uint32_t resolution_bits;
    uint32_t raw_words_per_row;
    uint32_t max_fifo_rows;
    uint32_t max_adc_rate_hz; // Conversions/s per physical ADC, shared by its channels.
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
};

const Model *find_model(uint32_t device_id) noexcept;
int open_backend(const std::string &path, std::unique_ptr<Backend> &backend);

} // namespace aioaio
