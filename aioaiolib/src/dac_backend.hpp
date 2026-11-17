#pragma once

#include <cstdint>

namespace aioaio {

struct DacCapabilities {
    uint32_t channels = 0;
    uint32_t fifo_samples = 0; // Scalar tagged DAC words, shared by all channels.
    uint32_t firmware_revision = 0; // Raw FPGA register value.
    double max_sample_rate = 0; // Aggregate DAC words/s, not waveform points/s.
};

} // namespace aioaio
