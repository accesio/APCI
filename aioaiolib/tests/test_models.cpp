#include "backend.hpp"
#include <iostream>

int main() {
    // Pure model lookup only. Never call open_backend or hardware operations.
    struct Expected { uint32_t id, adcs, bits, max_rate; };
    for (const auto model : {Expected{0xc2ec, 2, 16, 1000000}, Expected{0xc0e8, 2, 16, 1000000},
                             Expected{0xc25c, 2, 12, 500000}, Expected{0xc0ec, 1, 16, 1000000},
                             Expected{0x805d, 1, 12, 250000}, Expected{0xcee8, 1, 16, 1000000},
                             Expected{0x805e, 1, 12, 100000}, Expected{0xc05e, 1, 12, 100000},
                             Expected{0xc0ed, 1, 16, 500000}, Expected{0x80ee, 1, 16, 250000},
                             Expected{0xc0eb, 2, 16, 1000000}, Expected{0xce9b, 1, 12, 100000}}) {
        const auto *found = aioaio::find_model(model.id);
        if (!found || found->adc_count != model.adcs || found->resolution_bits != model.bits ||
            found->raw_words_per_row != 2 || found->channels_per_adc != 8 ||
            found->max_adc_rate_hz != model.max_rate) return 1;
    }
    // Prototype and non-ADC device IDs must not silently use a native model.
    for (const uint32_t id : {0xc2e8u, 0xc2e9u, 0xc2eau, 0x82e8u, 0x82e9u, 0x82eau,
                              0xc258u, 0xc259u, 0xc25au, 0x8258u, 0x8259u, 0x825au,
                              0x4ce8u, 0x4cd8u, 0xffffu}) {
        if (aioaio::find_model(id)) return 2;
    }
    std::cout << "Models: native geometry, ADC rate limits, and prototype exclusion verified without opening devices\n";
}
